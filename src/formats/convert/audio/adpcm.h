#pragma once

#include <cstdint>

namespace sh::convert::audio {

// The two previous output samples of one ADPCM channel: the saturated 16-bit values, except for an XA decoder
// asked to keep them unclamped (xa_adpcm.h).
struct AdpcmState {
    std::int32_t h1 = 0;
    std::int32_t h2 = 0;
};

inline std::int16_t saturate16(std::int32_t v) {
    return static_cast<std::int16_t>(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
}

// Prediction filters in 1/64 units, shared by SPU-ADPCM and XA-ADPCM: y = x + (h1 * k0 + h2 * k1) / 64.
inline constexpr std::int32_t kAdpcmK0[5] = {0, 60, 115, 98, 122};
inline constexpr std::int32_t kAdpcmK1[5] = {0, 0, -52, -55, -60};

}  // namespace sh::convert::audio
