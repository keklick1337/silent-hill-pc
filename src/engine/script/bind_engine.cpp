// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#include "engine/script/bind_engine.h"

#include "engine/version/engine_version.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include <tuple>

namespace sh::script {

namespace {

int l_at_least(lua_State* L) {
    const lua_Integer major = luaL_checkinteger(L, 1);
    const lua_Integer minor = luaL_optinteger(L, 2, 0);
    const lua_Integer patch = luaL_optinteger(L, 3, 0);
    const std::tuple<lua_Integer, lua_Integer, lua_Integer> engine{version::kMajor, version::kMinor, version::kPatch};
    lua_pushboolean(L, engine >= std::tuple{major, minor, patch} ? 1 : 0);
    return 1;
}

}  // namespace

void open_engine_api(lua_State* L) {
    lua_createtable(L, 0, 6);
    lua_pushlstring(L, version::kText.data(), version::kText.size());
    lua_setfield(L, -2, "version");
    lua_pushinteger(L, version::kMajor);
    lua_setfield(L, -2, "major");
    lua_pushinteger(L, version::kMinor);
    lua_setfield(L, -2, "minor");
    lua_pushinteger(L, version::kPatch);
    lua_setfield(L, -2, "patch");
    lua_pushstring(L, version::full());
    lua_setfield(L, -2, "build");
    lua_pushcfunction(L, l_at_least);
    lua_setfield(L, -2, "at_least");
    lua_setfield(L, -2, "engine");
}

}  // namespace sh::script
