// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/script/bind_flags.h"

#include "engine/script/caller_mod.h"
#include "engine/script/host_interfaces.h"
#include "engine/script/script_host.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace sh::script {

namespace {

FlagsHost* g_flags = nullptr;

Host* host_of(lua_State* L) { return static_cast<Host*>(lua_touserdata(L, lua_upvalueindex(1))); }

// A flag name ("M1S03_ShotgunShells0", "mymod:door_opened") or the save bit number.
int flag_id(lua_State* L, int arg) {
    if (lua_type(L, arg) != LUA_TSTRING) {
        return static_cast<int>(luaL_checkinteger(L, arg));
    }
    const char* name = lua_tostring(L, arg);
    const auto find = host_of(L)->game_api().flag_number;
    const int number = find != nullptr ? find_scoped(L, name, find) : -1;
    if (number < 0) {
        return luaL_error(L, "unknown %s '%s'", "flag", name);
    }
    return number;
}

// The legacy path: GameApi function pointers (tests and older game layers) behind the interface.
class GameApiFlags final : public FlagsHost {
public:
    explicit GameApiFlags(const GameApi& api) : api_(api) {}
    int get(int id) override { return api_.flag_get != nullptr && api_.flag_get(id) != 0; }
    void set(int id) override { if (api_.flag_set != nullptr) api_.flag_set(id, 1); }
    void clear(int id) override { if (api_.flag_set != nullptr) api_.flag_set(id, 0); }
    void set_alt(int id) override { if (api_.flag_set_alt != nullptr) api_.flag_set_alt(id); }

private:
    const GameApi& api_;
};

bool available(lua_State* L, FlagsHost*& out, GameApiFlags& legacy, bool (*present)(const GameApi&), const char* what) {
    if (g_flags != nullptr) {
        out = g_flags;
        return true;
    }
    if (present(host_of(L)->game_api())) {
        out = &legacy;
        return true;
    }
    luaL_error(L, "%s is not available", what);
    return false;
}

int l_get(lua_State* L) {
    GameApiFlags legacy(host_of(L)->game_api());
    FlagsHost* h = nullptr;
    available(L, h, legacy, [](const GameApi& a) { return a.flag_get != nullptr; }, "sh.flag_get");
    lua_pushboolean(L, h->get(flag_id(L, 1)));
    return 1;
}

int l_set(lua_State* L) {
    GameApiFlags legacy(host_of(L)->game_api());
    FlagsHost* h = nullptr;
    available(L, h, legacy, [](const GameApi& a) { return a.flag_set != nullptr; }, "sh.flag_set");
    const int id = flag_id(L, 1);
    luaL_checkany(L, 2);
    if (lua_toboolean(L, 2)) {
        h->set(id);
    } else {
        h->clear(id);
    }
    return 0;
}

int l_clear(lua_State* L) {
    GameApiFlags legacy(host_of(L)->game_api());
    FlagsHost* h = nullptr;
    available(L, h, legacy, [](const GameApi& a) { return a.flag_set != nullptr; }, "sh.flag.clear");
    h->clear(flag_id(L, 1));
    return 0;
}

int l_set_alt(lua_State* L) {
    GameApiFlags legacy(host_of(L)->game_api());
    FlagsHost* h = nullptr;
    available(L, h, legacy, [](const GameApi& a) { return a.flag_set_alt != nullptr; }, "sh.flag_set_alt");
    h->set_alt(flag_id(L, 1));
    return 0;
}

void push(lua_State* L, Host* host, lua_CFunction f) {
    lua_pushlightuserdata(L, host);
    lua_pushcclosure(L, f, 1);
}

}  // namespace

void set_flags_host(FlagsHost* host) { g_flags = host; }
FlagsHost* flags_host() { return g_flags; }

void install_flag_family(lua_State* L, Host* host) {
    struct Entry {
        const char* flat;
        const char* canonical;
        lua_CFunction fn;
    };
    const Entry entries[] = {
        {"flag_get", "get", l_get},
        {"flag_set", "set", l_set},
        {"flag_set_alt", "set_alt", l_set_alt},
        {nullptr, "clear", l_clear},
    };
    lua_newtable(L);  // sh.flag
    for (const Entry& e : entries) {
        push(L, host, e.fn);
        if (e.flat != nullptr) {
            lua_pushvalue(L, -1);
            lua_setfield(L, -4, e.flat);  // sh.<flat>
        }
        lua_setfield(L, -2, e.canonical);
    }
    lua_setfield(L, -2, "flag");
}

}  // namespace sh::script
