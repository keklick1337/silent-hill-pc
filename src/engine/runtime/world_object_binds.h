// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The Lua group sh.world_object.* for maps/<map>/world_objects.lua: a "script"
// object of the map's world_objects.json is moved, turned, re-modelled and shown or hidden at run time;
// position_get / rotation_get read any object's live pose. Positions Q12 metres, angles Q12 turns (16-bit, a turn
// count wraps).

#pragma once

#include "engine/script/sh_script.h"

namespace sh::world {

// Chains the group in front of the api's named calls.
void bind_world_object_api(ShScriptGameApi* api);

}  // namespace sh::world
