// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#pragma once

// The sh.render.* binds of the engine (render_binds.cpp).

#include <cstdint>

namespace sh::script {

// The effect-slot draw hook of a map (the effect frame calls it for each slot whose drawer is the map script):
// world_objects.lua's effect_slot_draw(slot, kind) runs with sh.render.world_quads drawing in the effects view, whose
// origin is (x, y, z) in geometry units (the player's position in whole metres).
void draw_effect_slot_script(int slot, std::int32_t origin_x, std::int32_t origin_y, std::int32_t origin_z);

}  // namespace sh::script
