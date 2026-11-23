// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#pragma once

struct lua_State;

namespace sh::script {

class Host;

// Pushes sh.console.{register, unregister, print, exec} into the sh table on top of the stack. Commands go
// into the process-wide registry (engine/console/instance.h) under a per-host owner.
void install_console_family(lua_State* L, Host* host);

// Called before the host's Lua state closes: removes every command this host registered.
void remove_console_family(Host* host);

}  // namespace sh::script
