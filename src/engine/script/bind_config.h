// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// sh.config.*: the Lua face of the mod settings store (mod_settings.h), in the menu state
// (bind_menu.cpp) and in the game's script state (script_host.cpp).
#pragma once

#include "engine/menu/menu_model.h"

#include <functional>
#include <string>

struct lua_State;

namespace sh::script {

class Host;

struct ConfigScope {
    std::function<void(int level, const std::string& message)> log;  // empty: stderr
    // Turns the function at `idx` into a change listener; empty: sh.config.on_change is refused
    // (a state that can be closed must not leave listeners behind in the process-wide store).
    std::function<std::function<void(const sh::menu::Value&)>(lua_State* L, int idx)> listener;
};

// Sets sh.config = {declare, get, set, default, reset, on_change, needs_restart} in the table on
// top of the stack. `scope` must outlive the state.
void install_config_table(lua_State* L, ConfigScope* scope);

// The game script state's sh.config: everything but on_change, errors in the host's log.
void install_config_family(lua_State* L, Host* host);

}  // namespace sh::script
