#include "formats/convert/audio/audio_converter.h"

#include "formats/convert/audio/json_writer.h"
#include "formats/convert/audio/wav.h"
#include "formats/convert/audio/xa_adpcm.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <tuple>
#include <string>

namespace sh::convert::audio {
namespace {

bool starts_with_nocase(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), text.begin(), [](char a, char b) {
               return std::toupper(static_cast<unsigned char>(a)) == std::toupper(static_cast<unsigned char>(b));
           });
}

std::string track_name(std::string_view game_path) {
    const std::size_t slash = game_path.find_last_of('/');
    const std::string_view name = slash == std::string_view::npos ? game_path : game_path.substr(slash + 1);
    return name.empty() ? std::string("xa") : std::string(name);
}

// One run of ADPCM sectors with the same (file, channel) and the same coding.
struct Stream {
    std::uint8_t file = 0;
    std::uint8_t channel = 0;
    std::uint8_t coding_byte = 0;
    XaCoding coding;
    unsigned segment = 0;  // 0 unless the coding of the channel changed in the middle of the track
    AdpcmState state[2];
    std::vector<std::int16_t> pcm;
    std::size_t sectors = 0;
    std::size_t first_sector = 0;
    std::size_t last_sector = 0;
    std::vector<std::uint32_t> sector_list;  // track sectors of the stream, in order
    bool eof = false;  // the last sector carries the end-of-file bit
};

struct Tally {
    std::size_t data_sectors = 0;          // video or other data, e.g. the STR sectors of a movie track
    std::size_t audio_without_form2 = 0;   // audio bit without the Form 2 bit: silent filler (file 255)
    std::size_t empty_sectors = 0;         // no data and no audio bit
    std::size_t reserved_coding = 0;       // Form 2 audio with a rate or depth the decoder does not define
    std::size_t subheader_copy_mismatch = 0;
    std::size_t emphasis_sectors = 0;
};

class XaConverter final : public Converter {
public:
    std::string_view name() const override { return "xa"; }
    bool lossless() const override { return false; }

    bool accepts(std::span<const std::uint8_t> raw, std::string_view game_path) const override {
        if (!starts_with_nocase(game_path, "XA/") || raw.empty() || raw.size() % kXaSectorBytes != 0) {
            return false;
        }
        for (std::size_t offset = 0; offset < raw.size(); offset += kXaSectorBytes) {
            if (is_xa_audio_submode(raw[offset + 2]) && parse_xa_coding(raw[offset + 3])) {
                return true;
            }
        }
        return false;
    }

    Result<std::vector<Output>> decode(std::span<const std::uint8_t> raw, std::string_view game_path) const override;

    Result<std::vector<std::uint8_t>> encode(std::span<const Output>) const override {
        return Result<std::vector<std::uint8_t>>::fail("xa: the WAV form is a representation, the raw track is not rebuilt from it");
    }
};

Result<std::vector<Output>> XaConverter::decode(std::span<const std::uint8_t> raw, std::string_view game_path) const {
    using R = Result<std::vector<Output>>;
    if (raw.size() % kXaSectorBytes != 0) {
        return R::fail("xa: size is not a whole number of 2336-byte sectors");
    }
    const std::size_t sector_count = raw.size() / kXaSectorBytes;

    std::vector<Stream> streams;
    std::map<unsigned, std::size_t> open_stream;  // (file << 8 | channel) -> index in `streams`
    Tally tally;

    for (std::size_t s = 0; s < sector_count; ++s) {
        const auto sector = raw.subspan(s * kXaSectorBytes).first<kXaSectorBytes>();
        const std::uint8_t submode = sector[2];
        if (!is_xa_audio_submode(submode)) {
            if (submode & kXaSubmodeData) {
                ++tally.data_sectors;
            } else if (submode & kXaSubmodeAudio) {
                ++tally.audio_without_form2;
            } else {
                ++tally.empty_sectors;
            }
            continue;
        }
        const auto coding = parse_xa_coding(sector[3]);
        if (!coding) {
            ++tally.reserved_coding;
            continue;
        }
        if (!std::equal(sector.begin(), sector.begin() + 4, sector.begin() + 4)) {
            ++tally.subheader_copy_mismatch;
        }
        tally.emphasis_sectors += coding->emphasis;

        const unsigned key = (unsigned{sector[0]} << 8) | sector[1];
        auto it = open_stream.find(key);
        if (it == open_stream.end() || streams[it->second].coding != *coding) {
            Stream fresh;
            fresh.file = sector[0];
            fresh.channel = sector[1];
            fresh.coding_byte = sector[3];
            fresh.coding = *coding;
            fresh.first_sector = s;
            if (it != open_stream.end()) {
                fresh.segment = streams[it->second].segment + 1;
            }
            streams.push_back(std::move(fresh));
            open_stream[key] = streams.size() - 1;
            it = open_stream.find(key);
        }
        Stream& stream = streams[it->second];

        const std::size_t frames = xa_frames_per_sector(stream.coding);
        const std::size_t at = stream.pcm.size();
        stream.pcm.resize(at + frames * (stream.coding.stereo ? 2 : 1));
        decode_xa_sector(sector, stream.coding, stream.state, stream.pcm.data() + at);
        ++stream.sectors;
        stream.last_sector = s;
        stream.sector_list.push_back(static_cast<std::uint32_t>(s));
        stream.eof = (submode & kXaSubmodeEof) != 0;
    }
    if (streams.empty()) {
        return R::fail("xa: no ADPCM audio sectors");
    }

    std::stable_sort(streams.begin(), streams.end(), [](const Stream& a, const Stream& b) {
        return std::tie(a.file, a.channel, a.segment) < std::tie(b.file, b.channel, b.segment);
    });

    const std::string track = track_name(game_path);
    std::vector<Output> outputs;
    JsonWriter json;
    json.begin_object();
    json.field("format", "silent-hill-xa");
    json.field("source", std::string(game_path));
    json.field("track", track);
    json.field("sector_bytes", kXaSectorBytes);
    json.field("sectors", sector_count);
    json.field("note",
               "The sound engine plays voice lines from the WAVs. Sector numbers count from the first sector of the "
               "track. A stream is the ADPCM sectors of one (file, channel) pair in disc order; sector_runs lists them "
               "as [first, stride, count] runs, so a line that starts at a track sector starts at its ordinal in the "
               "stream times the frames of a sector.");
    json.key("streams");
    json.begin_array();
    for (const Stream& stream : streams) {
        std::string wav_name = track;
        if (streams.size() > 1) {
            char suffix[32];
            std::snprintf(suffix, sizeof(suffix), "_f%02u_ch%u", stream.file, stream.channel);
            wav_name += suffix;
            if (stream.segment > 0) {
                wav_name += "_s" + std::to_string(stream.segment);
            }
        }
        wav_name += ".wav";

        Wav wav;
        wav.channels = stream.coding.stereo ? 2 : 1;
        wav.sample_rate = stream.coding.sample_rate;
        const std::size_t frames = stream.pcm.size() / wav.channels;
        wav.samples = stream.pcm;
        auto bytes = write_wav(wav);
        if (!bytes) {
            return R::fail("xa: " + wav_name + ": " + bytes.error());
        }
        outputs.push_back(Output{"audio/xa/" + wav_name, std::move(bytes.value())});

        json.begin_object(true);
        json.field("file", stream.file);
        json.field("channel", stream.channel);
        json.field("segment", stream.segment);
        json.field("wav", wav_name);
        char coding_hex[8];
        std::snprintf(coding_hex, sizeof(coding_hex), "0x%02X", stream.coding_byte);
        json.field("coding", coding_hex);
        json.field("channels", wav.channels);
        json.field("sample_rate", wav.sample_rate);
        json.field("bits", stream.coding.bits);
        json.field("sectors", stream.sectors);
        json.field("first_sector", stream.first_sector);
        json.field("last_sector", stream.last_sector);
        json.field("eof", stream.eof);
        json.key("sector_runs");
        json.begin_array(true);
        for (std::size_t i = 0; i < stream.sector_list.size();) {
            const std::uint32_t first = stream.sector_list[i];
            std::uint32_t stride = i + 1 < stream.sector_list.size() ? stream.sector_list[i + 1] - first : 1;
            std::size_t n = 1;
            while (i + n < stream.sector_list.size() && stream.sector_list[i + n] == first + stride * n) {
                ++n;
            }
            if (n == 1) {
                stride = 1;
            }
            json.begin_array(true);
            json.value(first);
            json.value(stride);
            json.value(n);
            json.end_array();
            i += n;
        }
        json.end_array();
        json.field("frames", frames);
        json.key("duration_s");
        json.value_fixed(static_cast<double>(frames) / wav.sample_rate, 6);
        json.end_object();
    }
    json.end_array();
    json.key("skipped_sectors");
    json.begin_object(true);
    json.field("data", tally.data_sectors);
    json.field("audio_without_form2", tally.audio_without_form2);
    json.field("empty", tally.empty_sectors);
    json.field("reserved_coding", tally.reserved_coding);
    json.end_object();
    json.key("checks");
    json.begin_object(true);
    json.field("subheader_copy_mismatch", tally.subheader_copy_mismatch);
    json.field("emphasis_sectors", tally.emphasis_sectors);
    json.end_object();
    json.end_object();

    std::string text = json.take();
    outputs.push_back(Output{"audio/xa/" + track + ".xa.json", std::vector<std::uint8_t>(text.begin(), text.end())});
    return R::ok(std::move(outputs));
}

}  // namespace

std::unique_ptr<Converter> make_xa_converter() {
    return std::make_unique<XaConverter>();
}

}  // namespace sh::convert::audio
