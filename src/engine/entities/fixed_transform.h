// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Fixed-point bone transforms of the entity module.
//
// A bone transform is a 3x3 rotation in Q12 (4096 = 1.0, stored in 16 bits like the game keeps it) and a
// translation in geometry units (256 = 1 m, +Y down). compose() is the parent * local product with exactly the
// integer rules the game's results were made with: 64-bit sums of 16 x 16 products shifted right by 12 and cut to
// 32 bits, rotation entries saturated to 16 bits, the local translation taken as its low 16 bits. Logic reads
// these numbers (hand and weapon positions, hit spheres), so the rules are part of the behaviour, not precision.

#pragma once

#include <array>
#include <cstdint>

namespace sh::entities {

inline constexpr std::int32_t kQ12One = 4096;

struct BoneTransform {
    std::array<std::int16_t, 9> rotation{kQ12One, 0, 0, 0, kQ12One, 0, 0, 0, kQ12One};  // row-major, Q12
    std::array<std::int32_t, 3> translation{0, 0, 0};                                   // geometry units

    std::int16_t at(int row, int column) const { return rotation[static_cast<std::size_t>(row * 3 + column)]; }
    bool operator==(const BoneTransform&) const = default;
};

inline constexpr BoneTransform kIdentityTransform{};

// parent * local: the transform of `local` in the space `parent` is in.
BoneTransform compose(const BoneTransform& parent, const BoneTransform& local);

// `point` (geometry units; its low 16 bits, like the local translation in compose()) moved by `transform`.
std::array<std::int32_t, 3> transform_point(const BoneTransform& transform, const std::array<std::int32_t, 3>& point);

}  // namespace sh::entities
