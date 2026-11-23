// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Point helpers the player, the NPC behaviours and the map effects share: the heading and elevation between two
// points, the 64-bit fixed product, points of a bone's space in the world. Every result is the original game's to
// the last bit (replay): positions Q12 metres (+Y down), angles Q12 turns, bone space in geometry units (Q8).

#pragma once

#include "engine/math/fixed_geometry.h"

#include <cstdint>
#include <span>

namespace sh::world {

struct WorldPoint {
    std::int32_t x = 0, y = 0, z = 0;
};

// Heading from `from` to `to` in the low 16 bits (as an unsigned 16-bit angle), and the angle between the vertical
// and the line in the high 16 bits (1024: level; measured from the +Y axis, which points down).
std::int32_t heading_and_elevation(const WorldPoint& from, const WorldPoint& to);

// (a * b) >> shift with a 64-bit product, cut back to 32 bits (the high word's low bits shifted in).
constexpr std::int32_t mul_fixed(std::int32_t a, std::int32_t b, int shift) {
    const std::int64_t product = static_cast<std::int64_t>(a) * b;
    const auto hi = static_cast<std::uint32_t>(static_cast<std::uint64_t>(product) >> 32);
    const auto lo = static_cast<std::uint32_t>(product);
    return static_cast<std::int32_t>((hi << (32 - shift)) | (lo >> shift));
}

// A bone's world transform: rotation (Q12) and translation (geometry units).
struct BoneFrame {
    sh::math::Matrix3 rotation;
    sh::math::Vec3i translation;
};

// Points given in a bone's space (geometry units) placed in the world (Q12 metres); `out` must be as long as `in`.
void bone_points_to_world(const BoneFrame& bone, std::span<const sh::math::Vec3s> in, std::span<WorldPoint> out);

}  // namespace sh::world
