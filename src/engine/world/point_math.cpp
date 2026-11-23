// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/world/point_math.h"

#include "engine/world/fixed_math.h"
#include "engine/world/fixed_trig.h"

#include <bit>

namespace sh::world {

namespace {

// v² in Q12: the 64-bit square cut back to 32 bits.
std::uint32_t square_q12(std::int32_t v) {
    const std::int64_t s = static_cast<std::int64_t>(v) * v;
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(s) >> 12);
}

// The root of a Q12 value kept at Q12, from the mantissa table (the variant this caller of the game uses; a value
// with the top bit set reads as a large unsigned one).
std::int32_t root_q12(std::int32_t value) {
    if (value == 0) {
        return 0;
    }
    const int zeros = value > 0 ? (std::countl_zero(static_cast<std::uint32_t>(value)) & 0x1E) : 0;
    const int shift = zeros - 24;
    const auto bits = static_cast<std::uint32_t>(value);
    const std::uint32_t mantissa = shift >= 0 ? bits << shift : bits >> -shift;
    const auto root = static_cast<std::uint32_t>(detail::kSqrtMantissas[mantissa - 64]);
    const int back = (shift + 6) >> 1;
    return static_cast<std::int32_t>(back >= 0 ? root >> back : root << -back);
}

}  // namespace

std::int32_t heading_and_elevation(const WorldPoint& from, const WorldPoint& to) {
    const std::int32_t dx = static_cast<std::int32_t>(static_cast<std::uint32_t>(to.x) - static_cast<std::uint32_t>(from.x));
    const std::int32_t dz = static_cast<std::int32_t>(static_cast<std::uint32_t>(to.z) - static_cast<std::uint32_t>(from.z));
    const std::int32_t dy = static_cast<std::int32_t>(static_cast<std::uint32_t>(to.y) - static_cast<std::uint32_t>(from.y));
    const auto heading = static_cast<std::uint16_t>(fixed_atan2(dx, dz));
    const std::int32_t level = root_q12(static_cast<std::int32_t>(square_q12(dx) + square_q12(dz)));
    const auto elevation = static_cast<std::uint32_t>(fixed_atan2(level, dy));
    return static_cast<std::int32_t>((elevation << 16) | heading);
}

void bone_points_to_world(const BoneFrame& bone, std::span<const sh::math::Vec3s> in, std::span<WorldPoint> out) {
    for (std::size_t i = 0; i < in.size() && i < out.size(); ++i) {
        const sh::math::MatVec r =
            sh::math::matrix_vector(bone.rotation, in[i], sh::math::Offset::Translation, bone.translation);
        // Geometry units (Q8) to Q12.
        out[i] = {static_cast<std::int32_t>(static_cast<std::uint32_t>(r.full.x) << 4),
                  static_cast<std::int32_t>(static_cast<std::uint32_t>(r.full.y) << 4),
                  static_cast<std::int32_t>(static_cast<std::uint32_t>(r.full.z) << 4)};
    }
}

}  // namespace sh::world
