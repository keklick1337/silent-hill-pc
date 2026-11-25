#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace sh::convert::audio {

// `pcm` (interleaved frames of `channels` channels at `from` Hz) at `to` Hz, band-limited: a Kaiser-windowed sinc of
// 32 taps, cut below the lower of the two Nyquist frequencies. For encoding to Opus, which takes 48 kHz only; the
// mixer has its own resampling for playback. Frames before the first and after the last count as silence.
std::vector<std::int16_t> resample_pcm(std::span<const std::int16_t> pcm, int channels, std::uint32_t from,
                                       std::uint32_t to);

// A frame position at `from` Hz moved to `to` Hz (rounded to the nearest frame).
std::uint64_t resample_position(std::uint64_t frame, std::uint32_t from, std::uint32_t to);

}  // namespace sh::convert::audio
