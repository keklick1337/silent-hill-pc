// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The script calls of the combat module: a character's attack on another, its movement
// step against the world, its shape offsets, the attack table's damage and the game's square root. A character is
// its slot (0 = player, 1..kNpcSlotCount = NPC slot + 1). Answered by the engine functions over the views the game binds.
//
//   entity.attack(slot, request, attack, x, y, z, target_slot, yaw, pitch) -> result of request_attack()
//   entity.move(slot, push_x, push_z) -> code, offset x, y, z, ground height, ground type
//   entity.shape_offsets_update(slot)
//   entity.weapon_damage(attack) -> power of the attack table row
//   math.sqrt(value) -> the game's integer square root

#pragma once

#include "engine/combat/attack.h"
#include "engine/combat/movement.h"
#include "engine/script/sh_script.h"

namespace sh::combat {

// Result of combat_script_call for a name outside the group: the caller passes the call on.
constexpr int kNotCombatCall = -2;

int combat_script_call(CombatView& combat, MoveView& move, const char* name, int argc, const ShScriptArg* argv,
                       long long* out, int out_cap, char* err, int err_cap);

// Puts the group in front of the API's named calls (one binding per process; the views must outlive it).
void bind_combat_script(ShScriptGameApi& api, CombatView& combat, MoveView& move);

}  // namespace sh::combat
