// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/world/map_wall.h"

#include "engine/world/fixed_trig.h"

#include <array>

namespace sh::world {
namespace {

// The game's macros, spelled out (fixed_point.h / arithmetic.h). The products
// and sums are the game's 32-bit ones — they WRAP on overflow (the game builds
// with -fwrapv) — so the unsigned casts are behaviour, not paranoia.
constexpr std::int32_t wmul(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}
constexpr std::int32_t wadd(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
constexpr std::int32_t q12_mul(std::int32_t a, std::int32_t b) { return wmul(a, b) >> 12; }
constexpr std::int32_t fp_mul_precise(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 12);
}
constexpr std::int32_t clamp_low(std::int32_t x, std::int32_t low) { return x < low ? low : x; }

}  // namespace

WallShade wall_shade(std::int32_t angle) {
    WallShade out;
    const std::int32_t sin_a = fixed_sin(angle >> 2);
    const std::int32_t cos_a = fixed_cos(angle >> 2);
    for (int i = 0; i < 3; i++) {
        const std::int32_t slope = ((angle >> 4) + 0x80) * i;
        out.dim[i] = -fp_mul_precise(
            clamp_low((4096 - q12_mul(slope, sin_a)) - cos_a, 0), 10240 /* Q12(2.5) */);
        out.span[i] = fp_mul_precise(clamp_low(angle - (q12_mul(slope, cos_a) - 0x80), 0), 10240);
    }
    return out;
}

WallPoint wall_point(std::int32_t src_x, std::int32_t src_y, std::int32_t src_z,
                     std::int32_t rot_y, std::int32_t sweep, std::int32_t span, std::int32_t dim,
                     std::int32_t player_x, std::int32_t player_y, std::int32_t player_z) {
    const std::int32_t sin_sweep = fixed_sin(sweep);
    const std::int32_t cos_sweep = fixed_cos(sweep);
    const std::int32_t sin_rot = fixed_sin(rot_y);
    const std::int32_t cos_rot = fixed_cos(rot_y);

    WallPoint out;
    // The original's (u16) cast on the player's x term and plain y/z terms are
    // behaviour, not noise: a negative player x wraps by +65536 before the
    // Q8 subtraction.
    out.x = ((src_x + (wadd(wmul(q12_mul(span, sin_sweep), cos_rot),
                           -wmul(dim, sin_rot)) >>
                       12)) >>
             4) -
            static_cast<std::uint16_t>(player_x);
    out.y = ((src_y + q12_mul(span, cos_sweep)) >> 4) - player_y;
    out.z = ((src_z + ((wadd(wmul(q12_mul(span, sin_sweep), sin_rot), wmul(dim, cos_rot))) >> 12)) >> 4) -
             player_z;
    return out;
}

WallColors wall_colors(const std::uint8_t* base, std::int32_t arg3, std::int32_t arg2) {
    const std::int32_t lower = (arg3 >> 4) + (arg2 >> 5);
    WallColors out;
    for (int k = 0; k < 3; k++) {
        out.near_row[k] = static_cast<std::uint8_t>(clamp_low(static_cast<std::int32_t>(base[k]) - lower, 0));
        out.far_row[k] = static_cast<std::uint8_t>(clamp_low(static_cast<std::int32_t>(base[3 + k]) - lower, 0));
    }
    return out;
}

}  // namespace sh::world
