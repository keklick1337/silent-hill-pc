// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#pragma once

struct lua_State;

namespace sh::script {

class Host;

// Puts sh.world.pickup_create / pickup_remove / pickups into the sh table on top of the stack (the registry of
// src/engine/world/runtime_pickups.h).
void install_world_pickup_family(lua_State* L, Host* host);

}  // namespace sh::script
