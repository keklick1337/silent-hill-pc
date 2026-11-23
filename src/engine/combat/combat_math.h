// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Integer math of the combat code. Every function returns the original game's value to
// the last bit: positions Q19.12 metres, angles Q3.12 turns (4096 = 360 degrees), 32-bit products wrap like the
// game's arithmetic. Standard library and sh_world's fixed-point trigonometry only.

#pragma once

#include <cstdint>

namespace sh::combat {

struct Vec3 {
    std::int32_t x = 0, y = 0, z = 0;
    bool operator==(const Vec3&) const = default;
};

// (a * b) >> 12 in 32 bits (the product wraps).
constexpr std::int32_t mul_q12(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b)) >> 12;
}

// (a * b) >> 12 with a 64-bit product, truncated to 32 bits.
constexpr std::int32_t mul_q12_wide(std::int64_t a, std::int64_t b) {
    return static_cast<std::int32_t>((a * b) >> 12);
}

// Division by four rounded towards zero, as the compiler emits it for a signed `/ 4`.
constexpr std::int32_t quarter(std::int32_t v) {
    return (v < 0 ? v + 3 : v) >> 2;
}

// The angle folded into [-2048, 2048] (the upper half of the turn becomes negative), as a 16-bit angle.
constexpr std::int16_t signed_angle(std::int32_t angle) {
    const std::int32_t folded = angle & 0xFFF;
    return static_cast<std::int16_t>(folded > 2048 ? (folded | 0xF000) : folded);
}

// Square root of a Q12 value in Q12 from the game's 192-entry mantissa table (the combat code's own variant: it
// keeps the root at Q12 instead of shifting it to an integer). A negative input is read as a large unsigned one,
// as the original does.
std::uint32_t sqrt_q12_table(std::int32_t value);

// Square root of a Q12 value in Q12 the way the game's library computes it (a double root, truncated).
std::int32_t sqrt_q12(std::int32_t value);

// x² + y² + z² of Q12 components in Q12, each square taken in 64 bits and cut back to 32.
std::int32_t square_sum_q12(std::int32_t x, std::int32_t y, std::int32_t z);

// Horizontal distance between two points (Q12), with the library root.
std::int32_t planar_distance(const Vec3& from, const Vec3& to);

// Distance of the point p from the segment a-b in the XZ plane, or from the nearer end when the point is beyond
// it. `along` (optional) receives where along a-b the foot of the point lies, Q12 (0 at a, 4096 at b); it is only
// written for the inside case and the two end cases.
std::uint32_t point_segment_distance(std::int32_t px, std::int32_t pz, std::int32_t ax, std::int32_t az,
                                     std::int32_t bx, std::int32_t bz, std::int16_t* along);

// The coarse arc cosine table some map effects use: the angle (Q12) whose cosine is `value` / 1024, for `value`
// 0..1024 in steps of 32. Values outside are clamped to the table.
std::int32_t coarse_arc_cosine(std::int32_t value);

// A full turn split in `sectors` equal parts (0 counts as 1): the step between the pellets of a shot.
constexpr std::int32_t sector_angle(std::int32_t sectors) {
    return 4096 / (sectors <= 0 ? 1 : sectors);
}

// An XZ offset of the character's collision shapes turned by its heading `yaw` (Q12 angle).
struct PlanarOffset {
    std::int16_t x = 0, z = 0;
};
PlanarOffset rotate_offset(std::int16_t x, std::int16_t z, std::int32_t yaw);

// The displacement of one movement step: `move_speed` along `heading` and `fall_speed` downwards for
// `delta_time` (Q12 seconds), plus the push (x, z). Large steps are computed at reduced precision to stay in 32
// bits, exactly as the game does.
Vec3 step_offset(std::int32_t delta_time, std::int32_t move_speed, std::int16_t heading, std::int32_t fall_speed,
                 std::int32_t push_x, std::int32_t push_z);

}  // namespace sh::combat
