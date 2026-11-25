#include "engine/audio/music_converter.h"

#include "engine/audio/music_layers.h"
#include "engine/audio/synth.h"

#include "formats/common/crc32.h"
#include "formats/convert/audio/json_writer.h"
#include "formats/convert/audio/ogg_opus.h"
#include "formats/convert/audio/resample.h"
#include "formats/convert/audio/wav.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

namespace sh::audio {
namespace {

std::string upper(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

std::string crc_text(std::span<const std::uint8_t> bytes) {
    char crc[16];
    std::snprintf(crc, sizeof crc, "%08X", sh::crc32(bytes.data(), bytes.size()));
    return crc;
}

class MusicConverter final : public sh::convert::Converter {
public:
    MusicConverter(SiblingReader sibling, MusicOptions options, MusicFile file)
        : sibling_(std::move(sibling)), options_(options), file_(file) {}

    std::string_view name() const override { return "music"; }
    bool lossless() const override { return false; }

    bool accepts(std::span<const std::uint8_t> raw, std::string_view game_path) const override {
        return sibling_ && raw.size() >= 16 && raw[0] == 'K' && raw[1] == 'D' && raw[2] == 'T' && raw[3] == '1' &&
               upper(game_path).ends_with(".KDT");
    }

    Result<std::vector<sh::convert::Output>> decode(std::span<const std::uint8_t> raw,
                                                    std::string_view game_path) const override {
        using R = Result<std::vector<sh::convert::Output>>;
        const std::string path(game_path);
        const std::string stem = path.substr(0, path.size() - 4);
        const std::string vab_path = stem + (path.ends_with(".kdt") ? ".vab" : ".VAB");
        const auto vab_raw = sibling_(vab_path);
        if (!vab_raw) {
            return R::fail("music: no bank " + vab_path + " for " + path);
        }
        auto bank = parse_vab(*vab_raw);
        if (!bank) {
            return R::fail("music: " + vab_path + ": " + bank.error());
        }
        const std::size_t slash = stem.find_last_of('/');
        const std::string name = slash == std::string::npos ? stem : stem.substr(slash + 1);
        // The full mix: every layer of the song open, as the game plays it at its most intense.
        const auto layers = song_layers(upper(name));
        MusicOptions options = options_;
        if (layers) {
            open_layers(options, *layers);
        }
        auto song = parse_kdt(raw);
        if (!song) {
            return R::fail("music: " + path + ": " + song.error());
        }
        auto rendered = render_music(song.value(), bank.value(), options);
        if (!rendered) {
            return R::fail("music: " + path + ": " + rendered.error());
        }
        const RenderedMusic& m = rendered.value();

        sh::convert::audio::Wav wav;
        wav.channels = 2;
        wav.sample_rate = kOutputRate;
        wav.samples = m.pcm;
        if (m.loop_start && m.loop_end && *m.loop_end > *m.loop_start) {
            wav.sampler = sh::convert::audio::WavSampler{};
            wav.sampler->loop = sh::convert::audio::WavLoop{*m.loop_start, *m.loop_end - 1};
        }
        // The description counts in the frames of the file written: Opus is always 48 kHz.
        Timeline time{kOutputRate, m.frames(), m.loop_start, m.loop_end, m.spill_frames};
        const bool opus = file_ == MusicFile::Opus;
        if (opus) {
            const auto at = [](std::uint64_t frame) {
                return static_cast<std::uint32_t>(
                    sh::convert::audio::resample_position(frame, kOutputRate, sh::convert::audio::kOpusRate));
            };
            time = Timeline{sh::convert::audio::kOpusRate, at(m.frames()), std::nullopt, std::nullopt,
                            at(m.spill_frames)};
            if (m.loop_start) time.loop_start = at(*m.loop_start);
            if (m.loop_end) time.loop_end = at(*m.loop_end);
        }
        sh::convert::audio::OpusEncoding encoding;
        encoding.serial = sh::crc32(reinterpret_cast<const std::uint8_t*>(name.data()), name.size());
        encoding.comments.push_back("TITLE=" + name);
        auto file_bytes = opus ? sh::convert::audio::write_ogg_opus(wav, encoding) : sh::convert::audio::write_wav(wav);
        if (!file_bytes) {
            return R::fail("music: " + file_bytes.error());
        }
        const std::string file_name = name + (opus ? ".opus" : ".wav");

        const std::string json =
            describe(m, time, path, raw, vab_path, *vab_raw, opus ? "opus" : "wav", file_name, layers);

        std::vector<sh::convert::Output> out;
        out.push_back({"audio/music/" + file_name, std::move(file_bytes.value())});
        out.push_back({"audio/music/" + name + ".music.json", std::vector<std::uint8_t>(json.begin(), json.end())});
        return R::ok(std::move(out));
    }

    Result<std::vector<std::uint8_t>> encode(std::span<const sh::convert::Output>) const override {
        return Result<std::vector<std::uint8_t>>::fail("music: a rendering, the song is rebuilt from its .kdt.json");
    }

private:
    struct Timeline {
        std::uint32_t rate;
        std::uint32_t frames;
        std::optional<std::uint32_t> loop_start, loop_end;
        std::uint32_t spill_frames;
    };

    std::string describe(const RenderedMusic& m, const Timeline& time, const std::string& kdt_path,
                         std::span<const std::uint8_t> kdt, const std::string& vab_path,
                         std::span<const std::uint8_t> vab, const char* file_key, const std::string& file,
                         const std::optional<std::array<std::uint8_t, 16>>& layers) const {
        sh::convert::audio::JsonWriter j;
        j.begin_object();
        j.field("format", "sh-music/1");
        j.field(file_key, file);
        j.key("source");
        j.begin_object(true);
        j.field("kdt", kdt_path);
        j.field("kdt_crc32", crc_text(kdt));
        j.field("vab", vab_path);
        j.field("vab_crc32", crc_text(vab));
        j.end_object();
        j.field("sample_rate", time.rate);
        j.field("channels", 2);
        j.field("frames", time.frames);
        j.key("duration_s");
        j.value_fixed(static_cast<double>(time.frames) / time.rate, 3);
        j.key("loop");
        if (time.loop_start && time.loop_end) {
            j.begin_object(true);
            j.field("start", *time.loop_start);
            j.field("end", *time.loop_end);
            j.field("spill_frames", time.spill_frames);
            j.end_object();
        } else {
            j.null();
        }
        j.field("help",
                "Frames 0..loop.end are the first pass as the game plays it; the song then repeats loop.start..loop.end "
                "for ever. The spill_frames after loop.end are what the first pass still sounds (release tails, "
                "reverb) while the repeat starts: mix them over loop.start to loop seamlessly. Rendered by the "
                "engine's SPU model and sequencer with the game's settings below; channels "
                "that the song keeps at volume 0 are music layers the game fades in (layers: the layer of each MIDI channel, "
                "0 = never opened); the render opens every layer at volume 127, which is the game's full mix.");
        j.key("render");
        j.begin_object(true);
        j.field("song_volume", options_.song_volume);
        j.field("main_volume", options_.main_volume);
        j.field("reverb_mode", options_.reverb_mode);
        j.field("reverb_depth", options_.reverb_depth);
        j.field("timer_ticks", m.timer_ticks);
        j.field("voices_peak", m.voices_peak);
        j.end_object();
        j.key("layers");
        if (layers) {
            j.begin_array(true);
            for (const std::uint8_t l : *layers) {
                j.value(l);
            }
            j.end_array();
        } else {
            j.null();
        }
        j.key("channel_volume_at_loop");
        j.begin_array(true);
        for (const int v : m.channel_volume_at_loop) {
            j.value(v);
        }
        j.end_array();
        j.end_object();
        return j.take();
    }

    SiblingReader sibling_;
    MusicOptions options_;
    MusicFile file_;
};

}  // namespace

std::unique_ptr<sh::convert::Converter> make_music_converter(SiblingReader sibling, MusicOptions options,
                                                             MusicFile file) {
    return std::make_unique<MusicConverter>(std::move(sibling), options, file);
}

}  // namespace sh::audio
