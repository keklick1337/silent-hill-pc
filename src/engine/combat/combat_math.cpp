// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/combat/combat_math.h"

#include "engine/world/fixed_math.h"
#include "engine/world/fixed_trig.h"

#include <array>
#include <bit>
#include <cmath>

namespace sh::combat {

namespace {

using sh::world::fixed_atan2;
using sh::world::fixed_cos;
using sh::world::fixed_sin;

constexpr std::int32_t wrap_mul(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}

constexpr std::int32_t wrap_neg(std::int32_t v) {
    return static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(v));
}

constexpr std::int32_t wrap_abs(std::int32_t v) {
    return v < 0 ? wrap_neg(v) : v;
}

// x² at Q6 precision (the low six bits dropped first so the square stays in 32 bits).
constexpr std::int32_t square_q6(std::int32_t v) {
    const std::int32_t s = v >> 6;
    return wrap_mul(s, s);
}

// Planar length of (x, z) at Q6 precision, back in Q12.
std::int32_t planar_length_q6(std::int32_t x, std::int32_t z) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(
                                         sh::world::game_sqrt(square_q6(x) + square_q6(z)))
                                     << 6);
}

// |length * f| in Q12 with the sign taken from the 32-bit product, as the original folds it.
std::int32_t folded_product(std::int32_t length, std::int32_t f) {
    const std::int32_t p = mul_q12(length, f);
    return wrap_mul(length, f) < 0 ? wrap_neg(p) : p;
}

// Arc cosine in Q12 turns of value / 1024 at 32 steps, the last entries 0.
constexpr std::array<std::int16_t, 34> kCoarseArcCosine = {
    0x3F6, 0x3E1, 0x3CD, 0x3B8, 0x3A4, 0x38F, 0x37B, 0x366, 0x351, 0x33B, 0x326, 0x310,
    0x2FA, 0x2E4, 0x2CD, 0x2B6, 0x29E, 0x286, 0x26E, 0x254, 0x23A, 0x21F, 0x203, 0x1E6,
    0x1C7, 0x1A6, 0x183, 0x15D, 0x133, 0x102, 0xC5,  0x57,  0x0,   0x0,
};

}  // namespace

std::uint32_t sqrt_q12_table(std::int32_t value) {
    if (value == 0) {
        return 0;
    }
    // Leading zeros of a positive value rounded down to even; a negative value counts none.
    const int zeros = value > 0 ? (std::countl_zero(static_cast<std::uint32_t>(value)) & 0x1E) : 0;
    const int shift = zeros - 24;
    const std::uint32_t bits = static_cast<std::uint32_t>(value);
    const std::uint32_t mantissa = shift >= 0 ? bits << shift : bits >> -shift;
    const auto root = static_cast<std::uint32_t>(sh::world::detail::kSqrtMantissas[mantissa - 64]);
    const int back = (shift + 6) >> 1;
    return back >= 0 ? root >> back : root << -back;
}

std::int32_t sqrt_q12(std::int32_t value) {
    if (value <= 0) {
        return 0;
    }
    return static_cast<std::int32_t>(std::sqrt(static_cast<double>(value)) * 64.0);
}

std::int32_t square_sum_q12(std::int32_t x, std::int32_t y, std::int32_t z) {
    const auto square = [](std::int32_t v) {
        const std::int64_t s = static_cast<std::int64_t>(v) * v;
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(s) >> 12));
    };
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(square(x)) + static_cast<std::uint32_t>(square(y)) +
                                     static_cast<std::uint32_t>(square(z)));
}

std::int32_t planar_distance(const Vec3& from, const Vec3& to) {
    const std::int32_t dx = to.x - from.x;
    const std::int32_t dz = to.z - from.z;
    return sqrt_q12(static_cast<std::int32_t>(static_cast<std::uint32_t>(mul_q12_wide(dx, dx)) +
                                              static_cast<std::uint32_t>(mul_q12_wide(dz, dz))));
}

std::uint32_t point_segment_distance(std::int32_t px, std::int32_t pz, std::int32_t ax, std::int32_t az,
                                     std::int32_t bx, std::int32_t bz, std::int16_t* along) {
    const std::int32_t to_a = signed_angle(fixed_atan2(px - ax, pz - az));
    const std::int32_t to_b = signed_angle(fixed_atan2(px - bx, pz - bz));
    const std::int32_t edge = signed_angle(fixed_atan2(bx - ax, bz - az));
    const std::int32_t from_a = planar_length_q6(px - ax, pz - az);
    const std::int32_t from_b = planar_length_q6(px - bx, pz - bz);

    // The point's distance from the line through a and b.
    const std::int32_t off_line = folded_product(from_a, fixed_sin(to_a - edge));

    const std::int32_t far_end = from_b < from_a ? from_a : from_b;
    const std::int32_t near_end = from_b < from_a ? from_b : from_a;
    const std::int32_t edge_sq = square_q6(bx - ax) + square_q6(bz - az);

    // The foot of the perpendicular is inside the segment when the triangle has no obtuse angle at either end.
    if (square_q6(far_end) < square_q6(near_end) + edge_sq) {
        if (along != nullptr) {
            const std::int32_t on_a = folded_product(from_a, fixed_cos(to_a - edge));
            const std::int32_t on_b = folded_product(from_b, fixed_cos(to_b - edge));
            const std::int32_t total = on_a + on_b;
            // A zero total (all three points together) faults in the original; nothing depends on the value.
            *along = static_cast<std::int16_t>(total != 0 ? static_cast<std::int32_t>(
                                                                 static_cast<std::uint32_t>(on_a) << 12) /
                                                                 total
                                                           : 0);
        }
        return static_cast<std::uint32_t>(wrap_abs(off_line));
    }
    if (from_a < from_b) {
        if (along != nullptr) {
            *along = 0;
        }
        return static_cast<std::uint32_t>(wrap_abs(from_a));
    }
    if (along != nullptr) {
        *along = 4096;
    }
    return static_cast<std::uint32_t>(wrap_abs(from_b));
}

std::int32_t coarse_arc_cosine(std::int32_t value) {
    std::int32_t index = value >> 5;
    if (index < 0) {
        index = 0;
    } else if (index >= static_cast<std::int32_t>(kCoarseArcCosine.size())) {
        index = static_cast<std::int32_t>(kCoarseArcCosine.size()) - 1;
    }
    return kCoarseArcCosine[static_cast<std::size_t>(index)];
}

PlanarOffset rotate_offset(std::int16_t x, std::int16_t z, std::int32_t yaw) {
    const std::int32_t c = fixed_cos(yaw);
    const std::int32_t s = fixed_sin(yaw);
    PlanarOffset out;
    out.x = static_cast<std::int16_t>((wrap_mul(x, c) + wrap_mul(z, s)) >> 12);
    out.z = static_cast<std::int16_t>((wrap_mul(-x, s) + wrap_mul(z, c)) >> 12);
    return out;
}

Vec3 step_offset(std::int32_t delta_time, std::int32_t move_speed, std::int16_t heading, std::int32_t fall_speed,
                 std::int32_t push_x, std::int32_t push_z) {
    const std::int32_t distance = mul_q12_wide(delta_time, move_speed);
    // Outside the 16-bit range both factors lose two bits each and the product is shifted back up.
    const int coarse = static_cast<std::uint32_t>(distance + 32767) >= 65535u ? 4 : 0;
    const int half = coarse >> 1;
    const std::int32_t reduced = distance >> half;
    const auto widen = [coarse](std::int32_t v) {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(v) << coarse);
    };
    Vec3 out;
    out.x = widen(mul_q12_wide(reduced, fixed_sin(heading) >> half)) + push_x;
    out.z = widen(mul_q12_wide(reduced, fixed_cos(heading) >> half)) + push_z;
    out.y = mul_q12_wide(delta_time, fall_speed);
    return out;
}

}  // namespace sh::combat
