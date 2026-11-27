// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#pragma once

struct lua_State;

namespace sh::script {

class Host;

// Puts sh.light.carry / sh.light.drop into the sh table on top of the stack (the registry of
// src/engine/world/carried_lights.h).
void install_light_family(lua_State* L, Host* host);

}  // namespace sh::script
