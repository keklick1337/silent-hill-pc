// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/script/bind_player.h"

#include "engine/script/host_interfaces.h"
#include "engine/script/script_host.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace sh::script {

namespace {

PlayerHost* g_player = nullptr;
CameraHost* g_camera = nullptr;

Host* host_of(lua_State* L) { return static_cast<Host*>(lua_touserdata(L, lua_upvalueindex(1))); }

// The legacy path: GameApi function pointers (tests and older game layers) behind the interfaces.
class GameApiPlayer final : public PlayerHost {
public:
    explicit GameApiPlayer(const GameApi& a) : a_(a) {}
    void pos(int out[3]) override { if (a_.player_pos) a_.player_pos(out); }
    void set_pos(const int xyz[3]) override { if (a_.player_set_pos) a_.player_set_pos(xyz); }
    void freeze(int on) override { if (a_.player_freeze) a_.player_freeze(on); }
    void unfreeze(int idle) override { if (a_.player_unfreeze_set_idle) a_.player_unfreeze_set_idle(idle); }
    void flashlight(int on) override { if (a_.flashlight) a_.flashlight(on); }

private:
    const GameApi& a_;
};

class GameApiCamera final : public CameraHost {
public:
    explicit GameApiCamera(const GameApi& a) : a_(a) {}
    void pos(int w, int p[3], int args[7]) override { if (a_.camera_pos) a_.camera_pos(w, p, args); }
    void look(int w, int p[3], int args[7]) override { if (a_.camera_look) a_.camera_look(w, p, args); }

private:
    const GameApi& a_;
};

// Resolves the interface or raises "<what> is not available".
PlayerHost* player(lua_State* L, GameApiPlayer& legacy, bool (*present)(const GameApi&), const char* what) {
    if (g_player != nullptr) return g_player;
    if (present(host_of(L)->game_api())) return &legacy;
    luaL_error(L, "%s is not available", what);
    return nullptr;
}

CameraHost* camera(lua_State* L, GameApiCamera& legacy, bool (*present)(const GameApi&), const char* what) {
    if (g_camera != nullptr) return g_camera;
    if (present(host_of(L)->game_api())) return &legacy;
    luaL_error(L, "%s is not available", what);
    return nullptr;
}

int l_player_pos(lua_State* L) {
    GameApiPlayer legacy(host_of(L)->game_api());
    PlayerHost* h = player(L, legacy, [](const GameApi& a) { return a.player_pos != nullptr; }, "sh.player_pos");
    int xyz[3] = {0, 0, 0};
    h->pos(xyz);
    for (int v : xyz) lua_pushinteger(L, v);
    return 3;
}

int l_player_set_pos(lua_State* L) {
    GameApiPlayer legacy(host_of(L)->game_api());
    PlayerHost* h = player(L, legacy, [](const GameApi& a) { return a.player_set_pos != nullptr; }, "sh.player_set_pos");
    const int xyz[3] = {static_cast<int>(luaL_checkinteger(L, 1)), static_cast<int>(luaL_checkinteger(L, 2)),
                        static_cast<int>(luaL_checkinteger(L, 3))};
    h->set_pos(xyz);
    return 0;
}

int l_player_freeze(lua_State* L) {
    GameApiPlayer legacy(host_of(L)->game_api());
    PlayerHost* h = player(L, legacy, [](const GameApi& a) { return a.player_freeze != nullptr; }, "sh.player_freeze");
    luaL_checkany(L, 1);
    if (!lua_isnoneornil(L, 2)) {
        // sh.player_freeze(false, setIdle): exact unfreeze(setIdle) semantics.
        h->unfreeze(lua_toboolean(L, 2) ? 1 : 0);
        return 0;
    }
    h->freeze(lua_toboolean(L, 1) ? 1 : 0);
    return 0;
}

int l_flashlight(lua_State* L) {
    GameApiPlayer legacy(host_of(L)->game_api());
    PlayerHost* h = player(L, legacy, [](const GameApi& a) { return a.flashlight != nullptr; }, "sh.flashlight");
    luaL_checkany(L, 1);
    h->flashlight(lua_toboolean(L, 1) ? 1 : 0);
    return 0;
}

// x/y/z in metres (converted to Q12 here), a0..a6 the Q12 offset/accel/speed words verbatim, warp.
void camera_args(lua_State* L, int v[3], int args[7]) {
    for (int i = 0; i < 3; ++i) v[i] = static_cast<int>(luaL_checknumber(L, i + 1) * 4096.0);
    for (int i = 0; i < 7; ++i) args[i] = static_cast<int>(luaL_optinteger(L, i + 4, 0));
}

int l_camera_pos(lua_State* L) {
    GameApiCamera legacy(host_of(L)->game_api());
    CameraHost* h = camera(L, legacy, [](const GameApi& a) { return a.camera_pos != nullptr; }, "sh.camera_pos");
    int v[3] = {}, args[7] = {};
    camera_args(L, v, args);
    h->pos(lua_toboolean(L, 11) ? 1 : 0, v, args);
    return 0;
}

int l_camera_look(lua_State* L) {
    GameApiCamera legacy(host_of(L)->game_api());
    CameraHost* h = camera(L, legacy, [](const GameApi& a) { return a.camera_look != nullptr; }, "sh.camera_look");
    int v[3] = {}, args[7] = {};
    camera_args(L, v, args);
    h->look(lua_toboolean(L, 11) ? 1 : 0, v, args);
    return 0;
}

}  // namespace

void set_player_host(PlayerHost* host) { g_player = host; }
PlayerHost* player_host() { return g_player; }
void set_camera_host(CameraHost* host) { g_camera = host; }
CameraHost* camera_host() { return g_camera; }

void install_player_camera_families(lua_State* L, Host* host) {
    struct Entry {
        const char* group;
        const char* canonical;
        const char* flat;
        lua_CFunction fn;
    };
    const Entry entries[] = {
        {"player", "pos", "player_pos", l_player_pos},
        {"player", "set_pos", "player_set_pos", l_player_set_pos},
        {"player", "freeze", "player_freeze", l_player_freeze},
        {"player", "flashlight", "flashlight", l_flashlight},
        {"camera", "pos", "camera_pos", l_camera_pos},
        {"camera", "look", "camera_look", l_camera_look},
    };
    for (const Entry& e : entries) {
        lua_pushlightuserdata(L, host);
        lua_pushcclosure(L, e.fn, 1);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, e.flat);  // sh.<flat>
        if (lua_getfield(L, -2, e.group) != LUA_TTABLE) {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_setfield(L, -4, e.group);
        }
        lua_pushvalue(L, -2);
        lua_setfield(L, -2, e.canonical);
        lua_pop(L, 2);  // group table, function
    }
}

}  // namespace sh::script
