#pragma once

// Fixed-point helpers of the camera module. Every helper reproduces the integer result the game's
// camera arithmetic produced, including 32-bit wraparound and truncation, because the camera feeds
// the replay hashes.

#include <cstdint>

#include "engine/world/fixed_trig.h"

namespace sh::camera::fx {

// Constants as the game's macros evaluated them: multiply in single precision, truncate toward zero.
constexpr std::int32_t q12(float value) { return static_cast<std::int32_t>(value * 4096.0f); }
constexpr std::int32_t q8(float value) { return static_cast<std::int32_t>(value * 256.0f); }
constexpr std::int32_t angle(float degrees) { return static_cast<std::int16_t>(q12(degrees / 360.0f)); }

constexpr std::int32_t wrap_mul(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}

// Sign-extends the low 12 bits: a Q12 angle folded into [-2048, 2047].
constexpr std::int32_t wrap_angle(std::int32_t a) { return ((a & 0xFFF) ^ 0x800) - 0x800; }

// (a * b) >> 12 with a 32-bit product.
constexpr std::int32_t mul(std::int32_t a, std::int32_t b) { return wrap_mul(a, b) >> 12; }

// (a * b) >> 12 with a 64-bit product, cut to 32 bits.
constexpr std::int32_t mul_wide(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 12);
}

// a scaled by a float factor that is first converted to Q12 (single precision, truncated).
constexpr std::int32_t mul_by(std::int32_t a, float factor) { return mul_wide(a, q12(factor)); }

constexpr std::int32_t clamp(std::int32_t v, std::int32_t lo, std::int32_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

constexpr std::int32_t abs32(std::int32_t v) { return v < 0 ? -v : v; }

// Q4 map units <-> Q12.
constexpr std::int32_t from_q4(std::int32_t v) { return v << 8; }
constexpr std::int32_t to_q4(std::int32_t v) { return v >> 8; }

inline std::int32_t sin(std::int32_t a) { return world::fixed_sin(a); }
inline std::int32_t cos(std::int32_t a) { return world::fixed_cos(a); }
inline std::int32_t atan2(std::int32_t y, std::int32_t x) { return world::fixed_atan2(y, x); }
inline std::int32_t length(std::int32_t x, std::int32_t y, std::int32_t z) { return world::fixed_length3(x, y, z); }

}  // namespace sh::camera::fx
