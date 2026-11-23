// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The movie sound on its way to the output: the volume, and wide integer samples narrowed to 16 bits.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sh::fmv {

enum class SampleFormat { U8, S16, F32 };

// The console plays a movie's sound through the CD input of its sound chip at 80/128 of full scale, under the
// music that keeps playing; `console_level` keeps that balance, off the sound plays at the full `volume`.
inline constexpr float kConsoleMovieLevel = 80.0f / 128.0f;
float movie_gain(float volume, bool console_level);

// Scales samples in place (native byte order). A gain of 0.999 or more leaves them as they are.
void apply_gain(void* samples, std::size_t bytes, SampleFormat format, float gain);

// 24- or 32-bit little-endian integer samples to 16 bits: the top two bytes of each.
void narrow_to_s16(std::span<const std::uint8_t> samples, int bits, std::vector<std::int16_t>& out);

}  // namespace sh::fmv
