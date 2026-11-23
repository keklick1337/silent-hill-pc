// SPDX-License-Identifier: GPL-3.0-or-later
// 32-bit wrap-around integer helpers for the screens' fixed-point arithmetic (Q.12
// products are taken in 64 bits and truncated to 32, as the specification does).
// Internal to the module.

#pragma once

#include <cstdint>

namespace sh::screens::roll_math {

inline std::int32_t add(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}

inline std::int32_t sub(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}

inline std::int32_t mul(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}

inline std::int32_t shl(std::int32_t a, int n) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) << n);
}

// (a * b) >> 12 in 64 bits, truncated.
inline std::int32_t q12_mul(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * static_cast<std::int64_t>(b)) >> 12);
}

// float -> Q.12 the way the game's constants were made: multiply in float, truncate.
inline std::int32_t q12_from_float(float value) {
    volatile float scaled = value * 4096.0f;
    return static_cast<std::int32_t>(scaled);
}

}  // namespace sh::screens::roll_math
