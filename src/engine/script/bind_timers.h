// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#pragma once

struct lua_State;

namespace sh::script {

class Host;

// Pushes the timers family into the sh table on top of the stack: flat sys_timer_get/set,
// cutscene_timer_get/set, game_timer_update and sh.timer.{sys_get,sys_set,cutscene_get,
// cutscene_set,game_update} (one function each).
void install_timer_family(lua_State* L, Host* host);

}  // namespace sh::script
