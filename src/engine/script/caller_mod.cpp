// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/script/caller_mod.h"

#include <algorithm>
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

sh::world::ScriptOrigin caller_origin(lua_State* L) {
    sh::world::ScriptOrigin origin;
    lua_Debug ar;
    // Level 1 is the function that called the C function; a few more levels name who called that.
    for (int level = 1; level <= 4 && lua_getstack(L, level, &ar) != 0; ++level) {
        if (lua_getinfo(L, "Sl", &ar) == 0 || ar.source == nullptr || ar.source[0] != '@' || ar.currentline <= 0) {
            continue;  // a C function or a chunk without a file between two script frames
        }
        // '/' on every system: the modding tools show the path and match it against the mod roots.
        std::string file = ar.source + 1;
        std::replace(file.begin(), file.end(), '\\', '/');
        if (!origin.known()) {
            origin.file = file;
            origin.line = ar.currentline;
            continue;
        }
        const std::size_t slash = file.rfind('/');
        if (!origin.via.empty()) origin.via += " < ";
        origin.via += (slash == std::string::npos ? file : file.substr(slash + 1)) + ":" + std::to_string(ar.currentline);
    }
    return origin;
}

}  // namespace sh::script
