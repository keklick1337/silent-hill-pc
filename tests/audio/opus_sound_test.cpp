// Ogg Opus wherever a mod may give a WAV: a voice recording (voice_clip.h) and a sample of a bank (sound_bank.h).

#include "engine/audio/sound_bank.h"
#include "engine/audio/voice_clip.h"
#include "formats/convert/audio/ogg_opus.h"

#include "framework/sh_test.h"

#include <cmath>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> opus_tone(std::uint16_t channels, std::size_t frames, bool loop) {
    sh::convert::audio::Wav wav;
    wav.channels = channels;
    wav.sample_rate = sh::convert::audio::kOpusRate;
    for (std::size_t i = 0; i < frames * channels; ++i) {
        wav.samples.push_back(static_cast<std::int16_t>(9000.0 * std::sin(0.05 * static_cast<double>(i / channels))));
    }
    if (loop) {
        wav.sampler = sh::convert::audio::WavSampler{};
        wav.sampler->loop = sh::convert::audio::WavLoop{4800, static_cast<std::uint32_t>(frames - 1)};
    }
    return sh::convert::audio::write_ogg_opus(wav).value();
}

}  // namespace

SH_TEST(voice_clip_reads_ogg_opus_at_the_voice_rate) {
    // A second at 48 kHz becomes a second at 37.8 kHz.
    const auto clip = sh::audio::parse_voice_wav(opus_tone(2, 48000, false));
    SH_CHECK(clip && clip.value().channels == 2 && clip.value().rate == sh::audio::kVoiceRate);
    SH_CHECK(clip && clip.value().frames() == 37800);
    std::vector<std::uint8_t> broken = opus_tone(1, 9600, false);
    broken.resize(broken.size() - 10);
    const auto bad = sh::audio::parse_voice_wav(broken);
    SH_CHECK(!bad && bad.error().starts_with("Ogg Opus"));
}

SH_TEST(sound_bank_takes_an_ogg_opus_sample_with_its_loop) {
    const auto bytes = opus_tone(2, 9600, true);
    const std::string json = R"({"format": "silent-hill-vab", "bank": "T",
        "header": {"tone_tables": 1, "master_volume": 100, "master_pan": 64},
        "programs": [{"index": 0, "volume": 90, "pan": 64, "tone_count": 1, "tone_table": 0,
            "tones": [{"slot": 0, "sample": 1, "volume": 127, "pan": 64, "root_key": 60, "fine_tune": 0,
                       "key_min": 0, "key_max": 127, "mode": 4, "adsr1": "0x80FF", "adsr2": "0xDFF5"}]}],
        "samples": [{"number": 1, "file": "T/001.wav"}]})";
    auto bank = sh::audio::load_sound_bank(
        json, [&](std::string_view) { return sh::Result<std::vector<std::uint8_t>>::ok(bytes); });
    SH_CHECK(static_cast<bool>(bank));
    if (!bank) return;
    const auto sample = bank.value().sample(1);
    // Stereo is folded to mono, as for a WAV; the loop comes from the tags.
    SH_CHECK(sample && sample->rate == 48000 && sample->pcm.size() == 9600);
    SH_CHECK(sample && sample->loop_start && *sample->loop_start == 4800u);
}
