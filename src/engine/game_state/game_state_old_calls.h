// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The functions and records of other modules the engine side of the game-state API (game_state_*.cpp) reaches by
// their C names: the ground query of the collision code, the player model loads of the world graphics, the item
// add of the inventory code, the file table's region setup, a few frame and screen globals and the NPC spawn marks
// of the character pool. This is the narrow view of them with the engine's types (Q12 numbers and enum values are
// 32-bit integers, pointers to records are opaque or a layout-identical engine record).

#pragma once

#include "engine/app/loader/game_files.h"

#include <cstddef>
#include <cstdint>

namespace sh::game_state::old_calls {

// The ground under a point as the collision query fills it (12 bytes).
struct GroundSurface {
    std::int32_t height;  // Q12, +Y down
    std::int16_t tilt_x;  // Q12 angles of the slope
    std::int16_t tilt_z;
    std::int8_t type;     // ground material
    std::uint8_t reserved[3];
};
static_assert(sizeof(GroundSurface) == 12);

}  // namespace sh::game_state::old_calls

extern "C" {

// Collision.
void Collision_SurfaceGet(void* surface, std::int32_t x, std::int32_t z);

// Character pool (src/engine/chara/game/chara_pool.cpp): the console's spawns.
extern std::uint8_t g_PcNpcDebugSpawned[];

// Inventory: the special-item add that stacks what stacks.
void Inventory_AddSpecialItem(int item, int count);

// The player's block of the running save (src/engine/gameflow/game/map_events_host.cpp).
void SysWork_SavegameUpdatePlayer(void);

// Display mode; `interlaced` is the C code's 4-byte boolean.
void Screen_Refresh(std::int32_t width, std::int32_t interlaced);

}  // extern "C"
