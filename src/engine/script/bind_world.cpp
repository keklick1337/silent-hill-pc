// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/script/bind_world.h"

#include "engine/script/caller_mod.h"
#include "engine/script/host_interfaces.h"
#include "engine/script/script_host.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/state_names.h"
#include "engine/world/script_origin.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace sh::script {

namespace {

WorldQueryHost* g_world = nullptr;

constexpr int kSearchStep = 1024;      // Q12 0.25 m between rings
constexpr int kDefaultRadius = 8192;   // Q12 2 m
constexpr int kMaxRadius = 16 * 4096;  // a search wider than this is a mistake, not a query

WorldQueryHost* world(lua_State* L, const char* what) {
    if (g_world == nullptr) {
        luaL_error(L, "%s is not available", what);
    }
    return g_world;
}

int int_arg(lua_State* L, int i, const char* what) {
    if (!lua_isinteger(L, i)) {
        return luaL_error(L, "%s: argument %d must be an integer (Q12 units)", what, i);
    }
    return static_cast<int>(lua_tointeger(L, i));
}

int l_difficulty(lua_State* L) {
    WorldQueryHost* h = world(L, "sh.game.difficulty");
    if (!h->in_game()) {
        lua_pushnil(L);
        return 1;
    }
    const int d = h->difficulty();
    lua_pushstring(L, d < 0 ? "easy" : d > 0 ? "hard" : "normal");
    return 1;
}

// sh.game.screen() / state(): the top-level screen and the in-game state by name (state_names.h); the number
// when a state has no name.
void push_state(lua_State* L, std::string_view name, std::int32_t value) {
    if (name.empty()) {
        lua_pushinteger(L, value);
    } else {
        lua_pushlstring(L, name.data(), name.size());
    }
}

int l_screen(lua_State* L) {
    const std::int32_t v = sh::game::session().game_state();
    push_state(L, sh::gameflow::game_state_name(v), v);
    return 1;
}

int l_screen_prev(lua_State* L) {
    const std::int32_t v = sh::game::session().game.game_state_previous;
    push_state(L, sh::gameflow::game_state_name(v), v);
    return 1;
}

int l_state(lua_State* L) {
    const std::int32_t v = sh::game::session().sys_state();
    push_state(L, sh::gameflow::sys_state_name(v), v);
    return 1;
}

int step_level(lua_State* L, const char* what) {
    const lua_Integer level = luaL_optinteger(L, 1, 0);
    if (level < 0 || level > 2) {
        return luaL_error(L, "%s: level must be 0, 1 or 2", what);
    }
    return static_cast<int>(level);
}

int l_screen_step(lua_State* L) {
    lua_pushinteger(L, sh::game::session().game.game_steps[step_level(L, "sh.game.screen_step")]);
    return 1;
}

int l_state_step(lua_State* L) {
    lua_pushinteger(L, sh::game::session().system.flow.sys_steps[step_level(L, "sh.game.state_step")]);
    return 1;
}

int l_cutscene(lua_State* L) {
    lua_pushboolean(L, sh::game::session().cutscene_live() ? 1 : 0);
    return 1;
}

int l_facing(lua_State* L) {
    lua_pushinteger(L, world(L, "sh.player.facing")->player_facing() & 0xFFF);
    return 1;
}

int l_walkable(lua_State* L) {
    WorldQueryHost* h = world(L, "sh.world.walkable");
    const int x = int_arg(L, 1, "sh.world.walkable");
    const int z = int_arg(L, 2, "sh.world.walkable");
    int from[2] = {0, 0};
    const bool has_from = !lua_isnoneornil(L, 3);
    if (has_from) {
        from[0] = int_arg(L, 3, "sh.world.walkable");
        from[1] = int_arg(L, 4, "sh.world.walkable");
    }
    int y = 0;
    const bool ok = h->walkable(x, z, has_from ? from : nullptr, &y);
    lua_pushboolean(L, ok ? 1 : 0);
    lua_pushinteger(L, y);
    return 2;
}

int l_nearest_walkable(lua_State* L) {
    WorldQueryHost* h = world(L, "sh.world.nearest_walkable");
    const int x = int_arg(L, 1, "sh.world.nearest_walkable");
    const int z = int_arg(L, 2, "sh.world.nearest_walkable");
    const int radius = lua_isnoneornil(L, 3) ? kDefaultRadius : int_arg(L, 3, "sh.world.nearest_walkable");
    if (radius < 0 || radius > kMaxRadius) {
        return luaL_error(L, "sh.world.nearest_walkable: radius must be 0..%d (Q12)", kMaxRadius);
    }
    int from[2] = {0, 0};
    const bool has_from = !lua_isnoneornil(L, 4);
    if (has_from) {
        from[0] = int_arg(L, 4, "sh.world.nearest_walkable");
        from[1] = int_arg(L, 5, "sh.world.nearest_walkable");
    }
    int y = 0;
    const auto found = nearest_walkable(x, z, radius, kSearchStep, [&](int px, int pz) {
        return h->walkable(px, pz, has_from ? from : nullptr, &y);
    });
    if (!found) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, found->x);
    lua_pushinteger(L, found->z);
    lua_pushinteger(L, y);
    return 3;
}

// A table field: integer (or absent: `fallback`), else a Lua error naming it.
int field_int(lua_State* L, const char* key, int fallback, bool required) {
    const int t = lua_getfield(L, 1, key);
    int v = fallback;
    if (t == LUA_TNIL) {
        if (required) {
            lua_pop(L, 1);
            return luaL_error(L, "sh.entity.spawn: '%s' is required", key);
        }
    } else if (lua_isinteger(L, -1)) {
        v = static_cast<int>(lua_tointeger(L, -1));
    } else {
        lua_pop(L, 1);
        return luaL_error(L, "sh.entity.spawn: '%s' must be an integer (Q12 units)", key);
    }
    lua_pop(L, 1);
    return v;
}

std::string field_string(lua_State* L, const char* key, const char* fallback) {
    const int t = lua_getfield(L, 1, key);
    std::string v = fallback != nullptr ? fallback : "";
    if (t == LUA_TSTRING) {
        v = lua_tostring(L, -1);
    } else if (t != LUA_TNIL) {
        lua_pop(L, 1);
        luaL_error(L, "sh.entity.spawn: '%s' must be a string", key);
        return v;
    }
    lua_pop(L, 1);
    return v;
}

int l_spawn(lua_State* L) {
    WorldQueryHost* h = world(L, "sh.entity.spawn");
    luaL_checktype(L, 1, LUA_TTABLE);
    std::string chara = field_string(L, "chara", nullptr);
    if (chara.empty()) {
        return luaL_error(L, "sh.entity.spawn: 'chara' (a character id) is required");
    }
    // A short name from a mod's script means the mod's own character first.
    if (const Host* host = static_cast<const Host*>(lua_touserdata(L, lua_upvalueindex(1)));
        host != nullptr && host->game_api().chara_id_find != nullptr && chara.find(':') == std::string::npos) {
        if (const std::string mod = caller_mod(L); !mod.empty()) {
            const std::string own = mod + ":" + chara;
            if (host->game_api().chara_id_find(own.c_str()) >= 0) {
                chara = own;
            }
        }
    }
    const int x = field_int(L, "x", 0, true);
    const int z = field_int(L, "z", 0, true);
    const int rot = field_int(L, "rot", 0, false);
    const int state = field_int(L, "state", -1, false);
    const std::string ai = field_string(L, "ai", "auto");
    char err[160] = {};
    const int slot = h->spawn(chara.c_str(), x, z, rot, ai.c_str(), state, err, static_cast<int>(sizeof err));
    if (slot < 0) {
        lua_pushnil(L);
        lua_pushstring(L, err[0] != '\0' ? err : "the character could not be spawned");
        return 2;
    }
    sh::world::ScriptSpawn made;
    made.chara = chara;
    made.owner = caller_mod(L);
    made.x = x;
    made.z = z;
    made.origin = caller_origin(L);
    sh::world::note_script_spawn(slot, std::move(made));
    lua_pushinteger(L, slot);
    return 1;
}

void set_in_group(lua_State* L, const char* group, const char* name, lua_CFunction fn, Host* host) {
    if (lua_getfield(L, -1, group) != LUA_TTABLE) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, group);
    }
    lua_pushlightuserdata(L, host);
    lua_pushcclosure(L, fn, 1);
    lua_setfield(L, -2, name);
    lua_pop(L, 1);
}

}  // namespace

std::optional<WalkPoint> nearest_walkable(int x, int z, int radius, int step,
                                          const std::function<bool(int, int)>& walkable) {
    if (walkable(x, z)) {
        return WalkPoint{x, z};
    }
    if (step <= 0) {
        return std::nullopt;
    }
    for (int r = step; r <= radius; r += step) {
        // About one point per step of the ring's length, at least 8.
        const int n = std::max(8, static_cast<int>(std::ceil(2.0 * std::numbers::pi * r / step)));
        for (int i = 0; i < n; ++i) {
            const double a = 2.0 * std::numbers::pi * i / n;
            const int px = x + static_cast<int>(std::lround(std::sin(a) * r));
            const int pz = z + static_cast<int>(std::lround(std::cos(a) * r));
            if (walkable(px, pz)) {
                return WalkPoint{px, pz};
            }
        }
    }
    return std::nullopt;
}

void set_world_query_host(WorldQueryHost* host) { g_world = host; }

void install_world_family(lua_State* L, Host* host) {
    set_in_group(L, "game", "difficulty", l_difficulty, host);
    set_in_group(L, "game", "screen", l_screen, host);
    set_in_group(L, "game", "screen_prev", l_screen_prev, host);
    set_in_group(L, "game", "state", l_state, host);
    set_in_group(L, "game", "screen_step", l_screen_step, host);
    set_in_group(L, "game", "state_step", l_state_step, host);
    set_in_group(L, "game", "cutscene", l_cutscene, host);
    set_in_group(L, "player", "facing", l_facing, host);
    set_in_group(L, "world", "walkable", l_walkable, host);
    set_in_group(L, "world", "nearest_walkable", l_nearest_walkable, host);
    set_in_group(L, "entity", "spawn", l_spawn, host);
}

}  // namespace sh::script
