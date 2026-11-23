// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/chara/game/bone_ops.h"

#include <array>

#include "engine/camera/view_system.h"
#include "engine/world/chara_collision.h"

namespace sh::chara::game::bones {

sh::math::Matrix3 rotation_of(const BoneCoord& b) {
    sh::math::Matrix3 m;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            m.m[r][c] = b.local.m[r][c];
        }
    }
    return m;
}

void set_rotation(BoneCoord& b, const sh::math::Matrix3& m) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            b.local.m[r][c] = m.m[r][c];
        }
    }
}

void rotation_set(BoneCoord& b, std::int16_t x, std::int16_t y, std::int16_t z) {
    set_rotation(b, sh::math::rot_matrix_zxy_neg({x, y, z}));
}

void place_root(BoneCoord& b, const sh::entities::Vec3& position, const sh::entities::Angles& rotation) {
    b.world_current = 0;
    b.local.t[0] = position.x >> 4;
    b.local.t[1] = position.y >> 4;
    b.local.t[2] = position.z >> 4;
    rotation_set(b, rotation.x, rotation.y, rotation.z);
}

void turn_own(BoneCoord& b, const sh::math::Matrix3& m) {
    set_rotation(b, sh::math::mul_matrix(rotation_of(b), m));
}

void turn_parent(BoneCoord& b, const sh::math::Matrix3& m) {
    set_rotation(b, sh::math::mul_matrix(m, rotation_of(b)));
}

void scale_rows(BoneCoord& b, std::int32_t x, std::int32_t y, std::int32_t z) {
    set_rotation(b, sh::math::scale_matrix(rotation_of(b), {x, y, z}));
}

void scale_columns(BoneCoord* set, int bone, std::int32_t x, std::int32_t y, std::int32_t z) {
    std::array<std::array<std::int16_t, 3>, 3> m;
    BoneCoord& b = set[bone];
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            m[r][c] = b.local.m[r][c];
        }
    }
    sh::world::scale_columns(m, static_cast<std::int16_t>(x), static_cast<std::int16_t>(y),
                             static_cast<std::int16_t>(z));
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            b.local.m[r][c] = m[r][c];
        }
    }
    set->world_current = 0;
}

World world(BoneCoord& b) {
    const sh::camera::Matrix w = sh::camera::frame_world(&b);
    World out;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            out.rotation.m[r][c] = w.m[r][c];
        }
        out.t[r] = w.t[r];
    }
    return out;
}

}  // namespace sh::chara::game::bones
