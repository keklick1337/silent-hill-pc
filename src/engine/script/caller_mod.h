// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The mod a Lua call comes from, and the short names it writes: a short
// name in a mod's script means the mod's own thing first, then the base game's, then the one mod that has it.
#pragma once

#include "engine/world/script_origin.h"

#include <string>
#include <string_view>

struct lua_State;

namespace sh::script {

// `mod.id` of the environment of the Lua function that called the running C function (each mod's scripts run in an
// environment of their own); "" for the base game's scripts and for calls from C.
std::string caller_mod(lua_State* L);

// The file and line of the Lua code that called the running C function, with the callers above it; unknown when
// the call does not come from a script file.
sh::world::ScriptOrigin caller_origin(lua_State* L);

// Rule 1 of the short names for a lookup `find` (-1 / < 0: unknown): a name without ':' from a mod's script tries
// "<mod>:<name>" first, then `name` as it is (rules 2 and 3 are the lookup's own).
template <typename Find>
int find_scoped(lua_State* L, const char* name, Find&& find) {
    if (name != nullptr && std::string_view(name).find(':') == std::string_view::npos) {
        if (const std::string mod = caller_mod(L); !mod.empty()) {
            const std::string own = mod + ":" + name;
            if (const int n = find(own.c_str()); n >= 0) {
                return n;
            }
        }
    }
    return find(name);
}

}  // namespace sh::script
