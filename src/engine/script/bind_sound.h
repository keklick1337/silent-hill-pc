// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#pragma once

struct lua_State;

namespace sh::script {

class Host;

// Pushes the sound, music and spawn families into the sh table on top of the stack (flat names,
// canonical sh.audio.* / sh.chara.* aliases of the same functions, and the named group sh.sound.*).
void install_sound_families(lua_State* L, Host* host);

// A sound argument for other binds: an id, or a registry name (gamedata/sounds); an unknown
// name raises a Lua error.
int sound_arg(lua_State* L, int idx);
// The registry id of a sound name, -1 when unknown.
int sound_id_find(lua_State* L, const char* name);

}  // namespace sh::script
