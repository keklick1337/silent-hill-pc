// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The result records of the game's collision and ray queries and the extra colliders a character carries, as the
// character hosts read them. The world queries (engine/world/game/character_world.h) fill the same bytes as their own
// records: world_record() gives that view of a host's record.

#pragma once

#include <cstdint>

#include "engine/entities/character_record.h"
#include "engine/world/game/character_world.h"

namespace sh::chara::game {

// The ground under a point.
struct GroundSurface {
    std::int32_t height;  // Q12, +Y down
    std::int16_t tilt_x;
    std::int16_t tilt_z;
    std::int8_t kind;     // the floor material
};

// A wall test's answer: the step the walls let the mover make, the ground where it lands and the ceiling.
struct WallResult {
    sh::entities::Vec3 offset;
    GroundSurface surface;
    std::int32_t ceiling;
};

// A traced ray: whether it hit, the material, where it stopped, the character it met, the distance and the ground.
struct RayRecord {
    std::int8_t hit;
    std::uint8_t ground_kind;
    std::int8_t reserved[2];
    sh::entities::Vec3 target;
    void* character;
    std::int32_t distance;
    std::int32_t ground_height;
    std::int16_t facing;
};

// An extra collision cylinder a character carries (the record points at an array of them).
struct ExtraCollider {
    sh::entities::Vec3 position;
    std::int16_t bottom;
    std::int16_t top;
    std::int16_t radius;
    std::int8_t state;
    std::uint8_t reserved;
};

static_assert(sizeof(GroundSurface) == sizeof(sh::world::game::SurfaceRecord));
static_assert(sizeof(WallResult) == sizeof(sh::world::game::MoveRecord));
static_assert(sizeof(RayRecord) == sizeof(sh::world::game::RayTraceRecord));

inline sh::world::game::SurfaceRecord& world_record(GroundSurface& r) {
    return reinterpret_cast<sh::world::game::SurfaceRecord&>(r);
}
inline sh::world::game::MoveRecord& world_record(WallResult& r) {
    return reinterpret_cast<sh::world::game::MoveRecord&>(r);
}
inline sh::world::game::RayTraceRecord& world_record(RayRecord& r) {
    return reinterpret_cast<sh::world::game::RayTraceRecord&>(r);
}
inline sh::world::Vec3 world_vec(const sh::entities::Vec3& v) { return {v.x, v.y, v.z}; }

}  // namespace sh::chara::game
