// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Bone matrix math of the skeleton path: the coordinate rotation
// composition of Bone_CoordRotationSet. Plain int16/int32 in the game's matrix
// layout; the fixed-point arithmetic itself is fixed_geometry.h (RENDER.md
// section 5.3).

#pragma once

#include <cstdint>

namespace sh::world {

// A bone matrix in the game's layout: Q3.12 rotation rows in int16, int32 translation.
struct BoneMatrix {
    std::int16_t rot[3][3];
    std::int32_t trans[3];
};

// Bone_CoordRotationSet: compose a bone coordinate's rotation with a ZXY
// rotation (the camera-convention rot_matrix_zxy_neg). The angles truncate to
// int16 as the original's 16-bit vector store did; the translation is kept — the
// original's multiply only replaced the 3x3 part.
void bone_coord_rotation(BoneMatrix& coord, std::int32_t rot_x, std::int32_t rot_y,
                         std::int32_t rot_z);

}  // namespace sh::world
