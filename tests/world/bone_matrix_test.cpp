// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include "engine/world/bone_matrix.h"

#include <cstring>

using sh::world::BoneMatrix;

namespace {

BoneMatrix sample() {
    BoneMatrix m{};
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) m.rot[r][c] = static_cast<std::int16_t>(100 + r * 10 + c);
        m.trans[r] = 1000 + r;
    }
    return m;
}

}  // namespace

SH_TEST(bone_coord_rotation_identity_keeps_matrix) {
    BoneMatrix m = sample();
    const BoneMatrix before = m;
    // Zero angles compose with the identity rotation: nothing changes.
    sh::world::bone_coord_rotation(m, 0, 0, 0);
    SH_CHECK(std::memcmp(m.rot, before.rot, sizeof m.rot) == 0);
    // The translation is never touched by the rotation composition.
    SH_CHECK(std::memcmp(m.trans, before.trans, sizeof m.trans) == 0);
}

SH_TEST(bone_coord_rotation_truncates_angles_to_int16) {
    BoneMatrix a = sample();
    BoneMatrix b = sample();
    // The original stores the angles into 16-bit components: 0x1FFFF truncates to 0xFFFF.
    sh::world::bone_coord_rotation(a, 0, 0x1FFFF, 0);
    sh::world::bone_coord_rotation(b, 0, -1, 0);
    SH_CHECK(std::memcmp(a.rot, b.rot, sizeof a.rot) == 0);
}

SH_TEST(bone_coord_rotation_matches_manual_compose) {
    // 90-degree yaw: the composed rotation must be the first matrix with its
    // X/Z rows swapped and negated per the ZXY-negative convention.
    BoneMatrix m{};
    m.rot[0][0] = 1000; m.rot[0][1] = 100; m.rot[0][2] = 10;
    m.rot[1][0] = 2000; m.rot[1][1] = 200; m.rot[1][2] = 20;
    m.rot[2][0] = 3000; m.rot[2][1] = 300; m.rot[2][2] = 30;
    const BoneMatrix before = m;
    // Q12(90deg) yaw = 4096/4 = 1024 (rot_y is the third axis of the ZXY order).
    sh::world::bone_coord_rotation(m, /*rot_x*/ 0, /*rot_y*/ 1024, /*rot_z*/ 0);
    // rot_y=1024 => cos = 0, sin = 1 in the ZXY-negative matrix; composing by
    // columns: new col0 = old(col2 negated per convention)... assert the exact
    // expected values computed from the definition (columns of the rotation):
    SH_CHECK(m.rot[0][0] == -before.rot[0][2]);
    SH_CHECK(m.rot[1][0] == -before.rot[1][2]);
    SH_CHECK(m.rot[2][0] == -before.rot[2][2]);
    SH_CHECK(m.rot[0][2] == before.rot[0][0]);
    SH_CHECK(m.rot[1][2] == before.rot[1][0]);
    SH_CHECK(m.rot[2][2] == before.rot[2][0]);
    SH_CHECK(m.rot[0][1] == before.rot[0][1]);
    SH_CHECK(m.rot[1][1] == before.rot[1][1]);
    SH_CHECK(m.rot[2][1] == before.rot[2][1]);
}
