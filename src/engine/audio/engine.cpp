#include "engine/audio/engine.h"

#include "engine/audio/sequencer.h"
#include "formats/common/json.h"
#include "formats/convert/audio/ogg_opus.h"
#include "formats/convert/audio/resample.h"
#include "formats/convert/audio/wav.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

namespace sh::audio {
namespace {

constexpr std::uint64_t kTickPeriod = static_cast<std::uint64_t>(kOutputRate) * kTimerDenominator;
constexpr std::size_t kStreamCacheSize = 6;

std::int16_t level_of(int library_volume) {
    return static_cast<std::int16_t>(std::clamp(library_volume, -0x4000, 0x3FFF) * 2);
}

std::string upper(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

std::string_view as_text(const std::vector<std::uint8_t>& bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::int64_t int_of(const json::Value& object, std::string_view key, std::int64_t fallback = 0) {
    const json::Value* v = object.find(key);
    return v && v->is_integer() ? v->integer() : fallback;
}

}  // namespace

std::int64_t VoiceTrackStream::ordinal(std::uint32_t sector) const {
    std::int64_t before = 0;
    for (const Run& r : runs) {
        if (sector >= r.first && r.stride != 0) {
            const std::uint32_t k = (sector - r.first) / r.stride;
            if (k < r.count && r.first + k * r.stride == sector) {
                return before + k;
            }
        }
        before += r.count;
    }
    return -1;
}

std::uint32_t VoiceTrackStream::sectors() const {
    std::uint32_t n = 0;
    for (const Run& r : runs) {
        n += r.count;
    }
    return n;
}

AudioEngine::AudioEngine() : effects_(effects_synth_) {
    effects_synth_.set_main_volume(0x3FFF, 0x3FFF);
    effects_synth_.set_reverb_mode(1);
    effects_synth_.set_reverb_depth(20 << 8, 20 << 8);
    effects_synth_.set_reverb_enabled(true);
}

void AudioEngine::set_reader(ConvertedFileReader reader) {
    std::lock_guard<std::mutex> lock(mutex_);
    reader_ = std::move(reader);
}

Result<std::shared_ptr<const SoundBank>> AudioEngine::bank_by_name(std::string_view name) {
    using R = Result<std::shared_ptr<const SoundBank>>;
    const std::string key = upper(name);
    if (auto it = banks_.find(key); it != banks_.end()) {
        return R::ok(it->second);
    }
    if (!reader_) {
        return R::fail("audio: no file reader");
    }
    auto json = reader_("audio/vab/" + key + ".vab.json");
    if (!json) {
        return R::fail(json.error());
    }
    auto bank = load_sound_bank(as_text(json.value()), [this](std::string_view relative) {
        return reader_("audio/vab/" + std::string(relative));
    });
    if (!bank) {
        return R::fail(bank.error());
    }
    auto shared = std::make_shared<const SoundBank>(std::move(bank.value()));
    banks_[key] = shared;
    return R::ok(shared);
}

Result<void> AudioEngine::load_bank(int slot, std::string_view name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto bank = bank_by_name(name);
    if (!bank) {
        effects_.set_bank(slot, nullptr);
        if (slot >= 0 && slot < kBankSlots) {
            slot_names_[static_cast<std::size_t>(slot)].clear();
        }
        return Result<void>::fail(bank.error());
    }
    effects_.set_bank(slot, bank.value());
    if (slot >= 0 && slot < kBankSlots) {
        slot_names_[static_cast<std::size_t>(slot)] = upper(name);
    }
    if (slot == music_slot_) {
        music_.set_bank(bank.value());
    }
    return Result<void>::ok();
}

void AudioEngine::close_bank(int slot) {
    std::lock_guard<std::mutex> lock(mutex_);
    effects_.set_bank(slot, nullptr);
    if (slot >= 0 && slot < kBankSlots) {
        slot_names_[static_cast<std::size_t>(slot)].clear();
    }
}

std::string AudioEngine::bank_name(int slot) {
    std::lock_guard<std::mutex> lock(mutex_);
    return slot >= 0 && slot < kBankSlots ? slot_names_[static_cast<std::size_t>(slot)] : std::string();
}

bool AudioEngine::bank_has_note(std::string_view name, int program, int note) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto bank = bank_by_name(name);
    if (!bank || program < 0 || program >= 128) {
        return false;
    }
    const SoundBank& b = *bank.value();
    const std::size_t table = b.table_of(program);
    const BankProgram& prog = b.programs[static_cast<std::size_t>(program)];
    for (int tone = 0; tone < prog.tones; ++tone) {
        const BankTone* atr = b.tone(table * 16 + static_cast<std::size_t>(tone));
        if (atr != nullptr && atr->sample != 0 && atr->key_min <= note && atr->key_max >= note) {
            return true;
        }
    }
    return false;
}

int AudioEngine::key_on_program(int slot, int program, int note, int fine, int left, int right) {
    std::lock_guard<std::mutex> lock(mutex_);
    return effects_.key_on_program(slot, program, note, fine, left, right);
}

int AudioEngine::key_on_tone(int slot, int program, int tone, int note, int fine, int left, int right) {
    std::lock_guard<std::mutex> lock(mutex_);
    return effects_.key_on_tone(slot, program, tone, note, fine, left, right);
}

int AudioEngine::key_on_voice(int voice, int slot, int program, int tone, int note, int fine, int left, int right) {
    std::lock_guard<std::mutex> lock(mutex_);
    return effects_.key_on_voice(voice, slot, program, tone, note, fine, left, right);
}

void AudioEngine::key_off_program(int slot, int program, int note, bool cut) {
    std::lock_guard<std::mutex> lock(mutex_);
    effects_.key_off_program(slot, program, note, cut);
}

int AudioEngine::key_off_voice(int voice, bool cut) {
    std::lock_guard<std::mutex> lock(mutex_);
    return effects_.key_off_voice(voice, cut);
}

KeyStatus AudioEngine::key_status(int voice) {
    std::lock_guard<std::mutex> lock(mutex_);
    return voice >= 0 && voice < kVoices ? effects_.key_status(voice) : KeyStatus::Off;
}

std::uint16_t AudioEngine::voice_pitch(int voice) {
    std::lock_guard<std::mutex> lock(mutex_);
    return voice >= 0 && voice < kVoices ? effects_.pitch(voice) : 0;
}

void AudioEngine::set_voice_pitch(int voice, std::uint16_t pitch) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (voice >= 0 && voice < kVoices) {
        effects_.set_pitch(voice, pitch);
    }
}

void AudioEngine::set_voice_volume(int voice, int left, int right) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (voice >= 0 && voice < kVoices) {
        effects_.set_volume(voice, left, right);
    }
}

std::uint32_t AudioEngine::take_released_voices() {
    std::lock_guard<std::mutex> lock(mutex_);
    return effects_.take_released_voices();
}

void AudioEngine::set_mono(bool mono) {
    std::lock_guard<std::mutex> lock(mutex_);
    effects_.set_mono(mono);
}

void AudioEngine::set_main_exempt(bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    effects_.set_main_exempt(on);
}

Result<void> AudioEngine::open_song(std::string_view name) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!reader_) {
        return Result<void>::fail("audio: no file reader");
    }
    auto json = reader_("audio/seq/" + upper(name) + ".kdt.json");
    if (!json) {
        return Result<void>::fail(json.error());
    }
    auto song = load_song(as_text(json.value()));
    if (!song) {
        return Result<void>::fail(song.error());
    }
    music_.open(std::make_shared<const Song>(std::move(song.value())));
    return Result<void>::ok();
}

void AudioEngine::song_play() {
    std::lock_guard<std::mutex> lock(mutex_);
    music_.play();
}

void AudioEngine::song_stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    music_.stop();
}

void AudioEngine::song_close() {
    std::lock_guard<std::mutex> lock(mutex_);
    music_.close();
}

void AudioEngine::set_song_volume(int left, int right) {
    std::lock_guard<std::mutex> lock(mutex_);
    music_.set_volume(left, right);
}

void AudioEngine::set_channel_volume(int channel, int volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    music_.set_channel_volume(channel, volume);
}

int AudioEngine::channel_volume(int channel) {
    std::lock_guard<std::mutex> lock(mutex_);
    return music_.channel_volume(channel);
}

void AudioEngine::set_main_volume(int left, int right) {
    std::lock_guard<std::mutex> lock(mutex_);
    main_left_ = static_cast<std::int16_t>(std::clamp(left << 7, -0x4000, 0x3FFF));
    main_right_ = static_cast<std::int16_t>(std::clamp(right << 7, -0x4000, 0x3FFF));
}

void AudioEngine::set_reverb_depth(int left, int right) {
    std::lock_guard<std::mutex> lock(mutex_);
    effects_synth_.set_reverb_depth(static_cast<std::int16_t>((left << 16) >> 8), static_cast<std::int16_t>((right << 16) >> 8));
    music_.set_reverb_depth(left, right);
}

void AudioEngine::set_voice_gain(float gain) {
    std::lock_guard<std::mutex> lock(mutex_);
    voice_gain_ = std::clamp(gain, 0.0f, 1.0f);
}

Result<std::vector<VoiceTrackStream>*> AudioEngine::track_by_name(std::string_view track) {
    using R = Result<std::vector<VoiceTrackStream>*>;
    std::string name(track);
    if (const std::size_t slash = name.find_last_of('/'); slash != std::string::npos) {
        name = name.substr(slash + 1);
    }
    name = upper(name);
    if (auto it = tracks_.find(name); it != tracks_.end()) {
        return R::ok(&it->second);
    }
    if (!reader_) {
        return R::fail("audio: no file reader");
    }
    auto bytes = reader_("audio/xa/" + name + ".xa.json");
    if (!bytes) {
        return R::fail(bytes.error());
    }
    auto doc = json::parse(as_text(bytes.value()));
    if (!doc) {
        return R::fail("voice track " + name + ": " + doc.error());
    }
    const json::Value* streams = doc.value().find("streams");
    if (!streams || !streams->is_array()) {
        return R::fail("voice track " + name + ": no streams");
    }
    std::vector<VoiceTrackStream> out;
    for (const json::Value& s : streams->array()) {
        VoiceTrackStream v;
        v.file = static_cast<int>(int_of(s, "file"));
        v.channel = static_cast<int>(int_of(s, "channel"));
        if (const json::Value* w = s.find("wav"); w && w->is_string()) {
            v.wav = w->string();
        }
        v.channels = static_cast<int>(int_of(s, "channels", 2));
        v.rate = static_cast<std::uint32_t>(int_of(s, "sample_rate", 37800));
        const int bits = static_cast<int>(int_of(s, "bits", 4));
        v.frames_per_sector = static_cast<std::uint32_t>((bits == 8 ? 2016 : 4032) / (v.channels == 1 ? 1 : 2));
        if (const json::Value* runs = s.find("sector_runs"); runs && runs->is_array()) {
            for (const json::Value& r : runs->array()) {
                if (r.is_array() && r.array().size() == 3) {
                    v.runs.push_back({static_cast<std::uint32_t>(r.array()[0].integer()),
                                      static_cast<std::uint32_t>(r.array()[1].integer()),
                                      static_cast<std::uint32_t>(r.array()[2].integer())});
                }
            }
        } else {
            // Older imports: the stream's sectors are evenly interleaved between its first and last.
            const auto first = static_cast<std::uint32_t>(int_of(s, "first_sector"));
            const auto last = static_cast<std::uint32_t>(int_of(s, "last_sector"));
            const auto count = static_cast<std::uint32_t>(int_of(s, "sectors"));
            const std::uint32_t stride = count > 1 ? (last - first) / (count - 1) : 1;
            if (count > 0 && first + stride * (count - 1) == last) {
                v.runs.push_back({first, stride ? stride : 1, count});
            }
        }
        if (int_of(s, "segment") == 0 && !v.wav.empty()) {
            out.push_back(std::move(v));
        }
    }
    auto [it, inserted] = tracks_.emplace(name, std::move(out));
    (void)inserted;
    return R::ok(&it->second);
}

std::shared_ptr<const std::vector<std::int16_t>> AudioEngine::stream_pcm(const std::string& wav, int& channels,
                                                                      std::uint32_t& rate) {
    if (auto it = stream_cache_.find(wav); it != stream_cache_.end()) {
        stream_order_.erase(std::find(stream_order_.begin(), stream_order_.end(), wav));
        stream_order_.push_back(wav);
        return it->second;
    }
    auto bytes = reader_("audio/xa/" + wav);
    if (!bytes) {
        return nullptr;
    }
    auto decoded = convert::audio::read_sound(bytes.value());
    if (!decoded) {
        return nullptr;
    }
    // A mod's Ogg Opus decodes at 48 kHz, but a line's frames are counted at the rate of the disc's stream.
    if (convert::audio::is_ogg(bytes.value()) && rate != 0 && decoded.value().sample_rate != rate) {
        convert::audio::Wav& wav = decoded.value();
        wav.samples = convert::audio::resample_pcm(wav.samples, wav.channels, wav.sample_rate, rate);
        wav.sample_rate = rate;
    }
    channels = decoded.value().channels;
    rate = decoded.value().sample_rate;
    auto pcm = std::make_shared<const std::vector<std::int16_t>>(std::move(decoded.value().samples));
    stream_cache_[wav] = pcm;
    stream_order_.push_back(wav);
    while (stream_order_.size() > kStreamCacheSize) {
        stream_cache_.erase(stream_order_.front());
        stream_order_.erase(stream_order_.begin());
    }
    return pcm;
}

bool AudioEngine::voice_start(std::string_view track, std::uint32_t sector, std::uint32_t length_vblanks) {
    std::lock_guard<std::mutex> lock(mutex_);
    voice_.stop();
    voice_pcm_.reset();
    voice_frames_ = 0;
    auto streams = track_by_name(track);
    if (!streams) {
        return false;
    }
    for (const VoiceTrackStream& s : *streams.value()) {
        const std::int64_t ordinal = s.ordinal(sector);
        if (ordinal < 0) {
            continue;
        }
        // The length of a line in sectors, as the game's player derives it from the authored length in VBlanks.
        const std::uint32_t samples_per_vblank = s.rate / 60u;
        const std::uint32_t samples_per_sector = s.channels == 2 ? 2016u : 4032u;
        const std::uint32_t wanted = (samples_per_vblank * length_vblanks + samples_per_sector - 1) / samples_per_sector;
        const std::uint32_t sectors = std::min<std::uint32_t>(wanted, s.sectors() - static_cast<std::uint32_t>(ordinal));
        int channels = s.channels;
        std::uint32_t rate = s.rate;
        auto pcm = stream_pcm(s.wav, channels, rate);
        if (!pcm) {
            return false;
        }
        voice_pcm_ = pcm;
        voice_channels_ = channels;
        voice_rate_ = rate;
        voice_first_ = static_cast<std::uint32_t>(ordinal) * s.frames_per_sector;
        voice_frames_ = sectors * s.frames_per_sector;
        voice_.start(pcm, channels, rate, voice_first_, voice_frames_);
        return voice_.active();
    }
    return false;
}

void AudioEngine::voice_start_pcm(std::shared_ptr<const std::vector<std::int16_t>> pcm, int channels,
                                  std::uint32_t rate, bool side) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::uint32_t frames = pcm ? static_cast<std::uint32_t>(pcm->size() / static_cast<std::size_t>(channels == 1 ? 1 : 2)) : 0;
    (side ? side_ : voice_).start(std::move(pcm), channels, rate, 0, frames);
}

void AudioEngine::voice_stop(bool side) {
    std::lock_guard<std::mutex> lock(mutex_);
    (side ? side_ : voice_).stop();
}

bool AudioEngine::voice_active(bool side) {
    std::lock_guard<std::mutex> lock(mutex_);
    return (side ? side_ : voice_).active();
}

bool AudioEngine::voice_drained(bool side) {
    std::lock_guard<std::mutex> lock(mutex_);
    return (side ? side_ : voice_).drained();
}

void AudioEngine::voice_set_paused(bool paused) {
    std::lock_guard<std::mutex> lock(mutex_);
    voice_.set_paused(paused);
}

void AudioEngine::set_voice_volume(int left, int right) {
    std::lock_guard<std::mutex> lock(mutex_);
    cd_left_ = std::clamp(left, 0, 0x7FFF);
    cd_right_ = std::clamp(right, 0, 0x7FFF);
}

void AudioEngine::set_voice_mix(bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    cd_mix_ = on;
}

void AudioEngine::timer_tick() {
    music_.timer_tick();
    if (timer_slow_ >= 11) {
        effects_.auto_release();
        timer_slow_ = 0;
    }
    ++timer_slow_;
}

bool AudioEngine::play_file(const std::filesystem::path& path, int tag, int left, int right, float semitones,
                            bool loop) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string key = path.string();
    auto it = file_cache_.find(key);
    if (it == file_cache_.end()) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return false;
        }
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto decoded = convert::audio::read_sound(bytes);
        if (!decoded) {
            return false;
        }
        FilePcm entry;
        entry.channels = decoded.value().channels;
        entry.rate = decoded.value().sample_rate;
        entry.pcm = std::make_shared<const std::vector<std::int16_t>>(std::move(decoded.value().samples));
        it = file_cache_.emplace(key, std::move(entry)).first;
    }
    files_.play(it->second.pcm, it->second.channels, it->second.rate, std::exp2(semitones / 12.0), left, right, loop,
                tag);
    ++files_started_;
    return true;
}

void AudioEngine::stop_file(int tag) {
    std::lock_guard<std::mutex> lock(mutex_);
    files_.stop(tag);
}

void AudioEngine::set_file_volume(int tag, int left, int right) {
    std::lock_guard<std::mutex> lock(mutex_);
    files_.set_volume(tag, left, right);
}

void AudioEngine::mix_block(std::int16_t* out, std::uint32_t frames) {
    left_.assign(frames, 0);
    right_.assign(frames, 0);
    fx_scratch_.resize(static_cast<std::size_t>(frames) * 2);
    fx_side_.resize(static_cast<std::size_t>(frames) * 2);
    effects_synth_.render_buses(fx_scratch_.data(), fx_side_.data(), frames);
    const auto fx = static_cast<std::int32_t>(effects_gain_ * 32768.0f);
    for (std::uint32_t i = 0; i < frames; ++i) {
        left_[i] = (fx_scratch_[2 * i] * fx) >> 15;
        right_[i] = (fx_scratch_[2 * i + 1] * fx) >> 15;
    }
    if (files_.active_count() > 0) {
        std::vector<std::int32_t> fl(frames, 0), fr(frames, 0);
        files_.mix(fl.data(), fr.data(), frames);
        for (std::uint32_t i = 0; i < frames; ++i) {
            left_[i] += (fl[i] * fx) >> 15;
            right_[i] += (fr[i] * fx) >> 15;
        }
    }
    if (music_gain_ >= 1.0f) {
        music_.render(left_.data(), right_.data(), frames);
    } else {
        std::vector<std::int32_t> ml(frames, 0), mr(frames, 0);
        music_.render(ml.data(), mr.data(), frames);
        const auto g = static_cast<std::int32_t>(music_gain_ * 32768.0f);
        for (std::uint32_t i = 0; i < frames; ++i) {
            left_[i] += (ml[i] * g) >> 15;
            right_[i] += (mr[i] * g) >> 15;
        }
    }
    // The CD input is consumed whether or not it is mixed: a muted line still ends on time.
    const auto vg = voice_gain_;
    const std::int32_t vl = cd_mix_ ? static_cast<std::int32_t>(cd_left_ * vg) : 0;
    const std::int32_t vr = cd_mix_ ? static_cast<std::int32_t>(cd_right_ * vg) : 0;
    voice_.mix(left_.data(), right_.data(), frames, vl, vr);
    side_.mix(left_.data(), right_.data(), frames, static_cast<std::int32_t>(0x7FFF * vg),
              static_cast<std::int32_t>(0x7FFF * vg));
    const std::int32_t ml = level_of(main_left_), mr = level_of(main_right_);
    for (std::uint32_t i = 0; i < frames; ++i) {
        const std::int32_t l = std::clamp(left_[i], -0x8000, 0x7FFF);
        const std::int32_t r = std::clamp(right_[i], -0x8000, 0x7FFF);
        // Voices keyed exempt are added after the main volume.
        out[2 * i] = static_cast<std::int16_t>(std::clamp(((l * ml) >> 15) + ((fx_side_[2 * i] * fx) >> 15), -0x8000, 0x7FFF));
        out[2 * i + 1] =
            static_cast<std::int16_t>(std::clamp(((r * mr) >> 15) + ((fx_side_[2 * i + 1] * fx) >> 15), -0x8000, 0x7FFF));
    }
}

void AudioEngine::render(std::int16_t* out, std::uint32_t frames) {
    std::lock_guard<std::mutex> lock(mutex_);
    while (frames > 0) {
        const std::uint64_t until_tick = (kTickPeriod - timer_phase_ + kTimerNumerator - 1) / kTimerNumerator;
        const auto n = static_cast<std::uint32_t>(std::min<std::uint64_t>(std::max<std::uint64_t>(until_tick, 1), frames));
        mix_block(out, n);
        out += static_cast<std::size_t>(n) * 2;
        frames -= n;
        timer_phase_ += static_cast<std::uint64_t>(n) * kTimerNumerator;
        while (timer_phase_ >= kTickPeriod) {
            timer_phase_ -= kTickPeriod;
            timer_tick();
        }
    }
}

AudioEngine& audio_engine() {
    static AudioEngine engine;
    return engine;
}

}  // namespace sh::audio
