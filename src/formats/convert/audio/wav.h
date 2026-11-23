#pragma once

#include "formats/common/status.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sh::convert::audio {

struct WavLoop {
    std::uint32_t start;  // first frame of the loop
    std::uint32_t end;    // last frame of the loop, inclusive (as in the smpl chunk)
};

// Sampler metadata of the smpl chunk.
struct WavSampler {
    std::uint8_t unity_note = 60;
    std::optional<WavLoop> loop;
};

// PCM16 audio. `samples` is interleaved: frame i, channel c is samples[i * channels + c].
struct Wav {
    std::uint16_t channels = 1;
    std::uint32_t sample_rate = 44100;
    std::vector<std::int16_t> samples;
    std::optional<WavSampler> sampler;

    std::size_t frames() const { return channels ? samples.size() / channels : 0; }
};

// RIFF/WAVE with a fmt chunk, a data chunk and, when `sampler` is set, a smpl chunk. Mono or stereo only.
Result<std::vector<std::uint8_t>> write_wav(const Wav& wav);

// Reads what write_wav writes, and other plain PCM16 files: unknown chunks are skipped, odd chunk sizes are padded.
Result<Wav> read_wav(std::span<const std::uint8_t> bytes);

}  // namespace sh::convert::audio
