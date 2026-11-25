// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A voice recording a mod supplies as a WAV or an Ogg Opus file, made ready for the voice-line input of the mixer
// (voice_stream.h), which takes the two rates of the disc's speech: 37.8 and 18.9 kHz.

#include "formats/common/status.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace sh::audio {

inline constexpr std::uint32_t kVoiceRate = 37800;
inline constexpr std::uint32_t kVoiceHalfRate = 18900;

struct VoiceClip {
    // Interleaved 16-bit frames.
    std::vector<std::int16_t> pcm;
    int channels = 1;
    std::uint32_t rate = kVoiceRate;

    std::uint32_t frames() const { return static_cast<std::uint32_t>(pcm.size() / static_cast<std::size_t>(channels)); }
};

// `frames` interleaved frames of `channels` channels at `rate`, brought to 37.8 kHz: each output frame lies
// between two input frames and is their straight-line blend; past the last input frame the last one holds.
std::vector<std::int16_t> resample_voice(std::span<const std::int16_t> pcm, int channels, std::uint32_t rate);

// The clip of a WAV file's bytes: integer PCM of 8 or 16 bits, mono or stereo, 4 to 96 kHz. A recording at
// one of the two voice rates is taken as it is, any other is resampled to 37.8 kHz. Ogg Opus (mono or stereo, by
// its first bytes) is decoded at 48 kHz and resampled the same way.
Result<VoiceClip> parse_voice_wav(std::span<const std::uint8_t> file);

// The same from a file of at most 64 MiB. A file that is not there fails with an empty message.
Result<VoiceClip> load_voice_wav(const std::filesystem::path& path);

}  // namespace sh::audio
