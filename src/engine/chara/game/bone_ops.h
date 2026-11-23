// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The bone edits the behaviours make on a posed skeleton between its animation and its draw: a rotation set from
// angles, a turn in the bone's own or its parent's space, scales, and the bone's world matrix. The fixed-point
// arithmetic is the geometry library's (src/engine/math/fixed_geometry.h); the translation of a bone is left alone unless
// named.

#pragma once

#include <cstdint>

#include "engine/entities/character_record.h"
#include "engine/math/fixed_geometry.h"

namespace sh::chara::game::bones {

using sh::entities::BoneCoord;

sh::math::Matrix3 rotation_of(const BoneCoord& b);
void set_rotation(BoneCoord& b, const sh::math::Matrix3& m);

// The rotation (angles Q12, order Z-X-Y in the camera convention) replaces the bone's.
void rotation_set(BoneCoord& b, std::int16_t x, std::int16_t y, std::int16_t z);
// The root bone placed at a record's position (Q12, kept in the bone's Q8 units) and rotation, marked fresh.
void place_root(BoneCoord& b, const sh::entities::Vec3& position, const sh::entities::Angles& rotation);
// The bone's rotation turned by m in its own space (rotation * m) / in its parent's space (m * rotation).
void turn_own(BoneCoord& b, const sh::math::Matrix3& m);
void turn_parent(BoneCoord& b, const sh::math::Matrix3& m);
// Rows of the rotation scaled by the Q12 factors.
void scale_rows(BoneCoord& b, std::int32_t x, std::int32_t y, std::int32_t z);
// Columns of bone `bone`'s rotation scaled (Q12); the first bone of the set is marked stale.
void scale_columns(BoneCoord* set, int bone, std::int32_t x, std::int32_t y, std::int32_t z);
// The bone's world matrix (the frames above it recomputed when stale).
struct World {
    sh::math::Matrix3 rotation;
    std::int32_t t[3];
};
World world(BoneCoord& b);

}  // namespace sh::chara::game::bones
