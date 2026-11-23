// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/world/bone_matrix.h"

#include "engine/math/fixed_geometry.h"

namespace sh::world {
namespace {

sh::math::Matrix3 rot_of(const BoneMatrix& b) {
    sh::math::Matrix3 m;
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            m.m[row][col] = b.rot[row][col];
        }
    }
    return m;
}

}  // namespace

void bone_coord_rotation(BoneMatrix& coord, std::int32_t rot_x, std::int32_t rot_y,
                         std::int32_t rot_z) {
    // The original stored the angles into a 16-bit vector first: int16 truncation.
    const sh::math::Matrix3 rotation =
        sh::math::rot_matrix_zxy_neg({static_cast<std::int16_t>(rot_x), static_cast<std::int16_t>(rot_y),
                                     static_cast<std::int16_t>(rot_z)});
    const sh::math::Matrix3 composed = sh::math::mul_matrix(rot_of(coord), rotation);
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            coord.rot[row][col] = composed.m[row][col];
        }
    }
}

}  // namespace sh::world
