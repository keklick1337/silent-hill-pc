// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/script/caller_mod.h"

#include <cstring>

extern "C" {
#include "lua.h"
}

namespace sh::script {

std::string caller_mod(lua_State* L) {
    lua_Debug ar;
    std::string owner;
    if (lua_getstack(L, 1, &ar) == 0 || lua_getinfo(L, "f", &ar) == 0) {
        return owner;
    }
    for (int i = 1;; ++i) {
        const char* name = lua_getupvalue(L, -1, i);
        if (name == nullptr) {
            break;
        }
        if (std::strcmp(name, "_ENV") == 0 && lua_istable(L, -1)) {
            // Raw: the base's globals behind the environment's __index have no `mod`.
            lua_pushliteral(L, "mod");
            if (lua_rawget(L, -2) == LUA_TTABLE) {
                lua_pushliteral(L, "id");
                if (lua_rawget(L, -2) == LUA_TSTRING) {
                    owner = lua_tostring(L, -1);
                }
                lua_pop(L, 1);
            }
            lua_pop(L, 2);
            break;
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return owner;
}

}  // namespace sh::script
