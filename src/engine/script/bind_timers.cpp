// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/script/bind_timers.h"

#include "engine/script/host_interfaces.h"
#include "engine/script/script_host.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace sh::script {

namespace {

TimerHost* g_timer = nullptr;

Host* host_of(lua_State* L) { return static_cast<Host*>(lua_touserdata(L, lua_upvalueindex(1))); }

// GameApi function pointers (tests and older game layers) behind the interface.
class GameApiTimer final : public TimerHost {
public:
    explicit GameApiTimer(const GameApi& a) : a_(a) {}
    int sys_get() override { return a_.sys_timer_get ? a_.sys_timer_get() : 0; }
    void sys_set(int v) override { if (a_.sys_timer_set) a_.sys_timer_set(v); }
    int cutscene_get() override { return a_.cutscene_timer_get ? a_.cutscene_timer_get() : 0; }
    void cutscene_set(int v) override { if (a_.cutscene_timer_set) a_.cutscene_timer_set(v); }
    void game_update() override { if (a_.game_timer_update) a_.game_timer_update(); }

private:
    const GameApi& a_;
};

TimerHost* timer(lua_State* L, GameApiTimer& legacy, bool (*present)(const GameApi&), const char* what) {
    if (g_timer != nullptr) return g_timer;
    if (present(host_of(L)->game_api())) return &legacy;
    luaL_error(L, "%s is not available", what);
    return nullptr;
}

#define TIMER_HOST(field, what) \
    GameApiTimer legacy(host_of(L)->game_api()); \
    TimerHost* h = timer(L, legacy, [](const GameApi& a) { return a.field != nullptr; }, what)

int l_sys_timer_get(lua_State* L) {
    TIMER_HOST(sys_timer_get, "sh.sys_timer_get");
    lua_pushinteger(L, h->sys_get());
    return 1;
}
int l_sys_timer_set(lua_State* L) {
    TIMER_HOST(sys_timer_set, "sh.sys_timer_set");
    h->sys_set(static_cast<int>(luaL_checkinteger(L, 1)));
    return 0;
}
int l_cutscene_timer_get(lua_State* L) {
    TIMER_HOST(cutscene_timer_get, "sh.cutscene_timer_get");
    lua_pushinteger(L, h->cutscene_get());
    return 1;
}
int l_cutscene_timer_set(lua_State* L) {
    TIMER_HOST(cutscene_timer_set, "sh.cutscene_timer_set");
    h->cutscene_set(static_cast<int>(luaL_checkinteger(L, 1)));
    return 0;
}
int l_game_timer_update(lua_State* L) {
    TIMER_HOST(game_timer_update, "sh.game_timer_update");
    h->game_update();
    return 0;
}

}  // namespace

void set_timer_host(TimerHost* host) { g_timer = host; }

void install_timer_family(lua_State* L, Host* host) {
    struct Entry {
        const char* canonical;
        const char* flat;
        lua_CFunction fn;
    };
    const Entry entries[] = {
        {"sys_get", "sys_timer_get", l_sys_timer_get},
        {"sys_set", "sys_timer_set", l_sys_timer_set},
        {"cutscene_get", "cutscene_timer_get", l_cutscene_timer_get},
        {"cutscene_set", "cutscene_timer_set", l_cutscene_timer_set},
        {"game_update", "game_timer_update", l_game_timer_update},
    };
    for (const Entry& e : entries) {
        lua_pushlightuserdata(L, host);
        lua_pushcclosure(L, e.fn, 1);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, e.flat);
        if (lua_getfield(L, -2, "timer") != LUA_TTABLE) {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_setfield(L, -4, "timer");
        }
        lua_pushvalue(L, -2);
        lua_setfield(L, -2, e.canonical);
        lua_pop(L, 2);
    }
}

}  // namespace sh::script
