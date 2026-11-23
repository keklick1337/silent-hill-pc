// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#pragma once

struct lua_State;

namespace sh::script {

class Host;

// Pushes the player and camera families into the sh table on top of the stack: flat player_pos,
// player_set_pos, player_freeze, flashlight, camera_pos, camera_look, and the canonical
// sh.player.{pos,set_pos,freeze,flashlight} / sh.camera.{pos,look} (one function each).
void install_player_camera_families(lua_State* L, Host* host);

}  // namespace sh::script
