// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// sh.light.carry(id, {slot=, bone=, position=, direction=, cone=, color=, intensity=, range=, radius=, shadows=,
//                     gi=}) -> true | nil, err
// sh.light.drop([id]) -> dropped
// A light a character carries: it follows one bone of the character in `slot` (0 the player) until it is dropped or
// the map changes. Metres and degrees, in the bone's own space (y down), as the lights of maps/<map>/lights.json.

#include "engine/script/bind_lights.h"

#include "engine/script/caller_mod.h"
#include "engine/world/carried_lights.h"

#include <array>
#include <string>
#include <utility>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace sh::script {

namespace {

namespace sw = sh::world;

int fail(lua_State* L, const char* why) {
    lua_pushnil(L);
    lua_pushstring(L, why);
    return 2;
}

// def[key] as a number; false when it is there and is not one.
bool number_field(lua_State* L, int def, const char* key, float& out) {
    const int type = lua_getfield(L, def, key);
    const bool ok = type == LUA_TNIL || type == LUA_TNUMBER;
    if (type == LUA_TNUMBER) out = static_cast<float>(lua_tonumber(L, -1));
    lua_pop(L, 1);
    return ok;
}

bool flag_field(lua_State* L, int def, const char* key, bool& out) {
    const int type = lua_getfield(L, def, key);
    const bool ok = type == LUA_TNIL || type == LUA_TBOOLEAN;
    if (type == LUA_TBOOLEAN) out = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return ok;
}

// def[key] as {a, b, c}; false when it is there and is not three numbers.
bool triple_field(lua_State* L, int def, const char* key, std::array<float, 3>& out) {
    const int type = lua_getfield(L, def, key);
    bool ok = type == LUA_TNIL || type == LUA_TTABLE;
    if (type == LUA_TTABLE) {
        std::array<float, 3> v{};
        for (int i = 0; i < 3 && ok; ++i) {
            ok = lua_rawgeti(L, -1, i + 1) == LUA_TNUMBER;
            if (ok) v[static_cast<std::size_t>(i)] = static_cast<float>(lua_tonumber(L, -1));
            lua_pop(L, 1);
        }
        if (ok) out = v;
    }
    lua_pop(L, 1);
    return ok;
}

int l_carry(lua_State* L) {
    sw::CarriedLight light;
    light.owner = caller_mod(L);
    light.id = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    float slot = 0.0f;
    if (!number_field(L, 2, "slot", slot) || slot < 0.0f) return fail(L, "slot is a character's slot (0: the player)");
    light.slot = static_cast<int>(slot);

    const int bone = lua_getfield(L, 2, "bone");
    if (bone == LUA_TNUMBER && lua_tointeger(L, -1) >= 0) {
        light.bone = static_cast<int>(lua_tointeger(L, -1));
    } else if (bone == LUA_TSTRING) {
        light.bone_name = lua_tostring(L, -1);
    } else if (bone == LUA_TNIL) {
        light.bone_name = "Torso";
    } else {
        lua_pop(L, 1);
        return fail(L, "bone is a bone index or a bone name of the player's skeleton");
    }
    lua_pop(L, 1);

    if (!triple_field(L, 2, "position", light.position)) return fail(L, "position is {x, y, z} in metres");
    if (!triple_field(L, 2, "direction", light.direction)) return fail(L, "direction is {x, y, z}");
    if (!triple_field(L, 2, "color", light.color)) return fail(L, "color is {r, g, b}");

    // cone: the outer half-angle, or {inner, outer}.
    const int cone = lua_getfield(L, 2, "cone");
    if (cone == LUA_TNUMBER) {
        light.cone_outer = static_cast<float>(lua_tonumber(L, -1));
        light.cone_inner = light.cone_outer * 0.6f;
    } else if (cone == LUA_TTABLE) {
        const bool inner = lua_rawgeti(L, -1, 1) == LUA_TNUMBER;
        if (inner) light.cone_inner = static_cast<float>(lua_tonumber(L, -1));
        const bool outer = lua_rawgeti(L, -2, 2) == LUA_TNUMBER;
        if (outer) light.cone_outer = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 2);
        if (!inner || !outer) {
            lua_pop(L, 1);
            return fail(L, "cone is degrees or {inner, outer}");
        }
    } else if (cone != LUA_TNIL) {
        lua_pop(L, 1);
        return fail(L, "cone is degrees or {inner, outer}");
    }
    lua_pop(L, 1);
    if (light.cone_outer <= 0.0f || light.cone_outer >= 90.0f || light.cone_inner < 0.0f ||
        light.cone_inner > light.cone_outer) {
        return fail(L, "cone: 0 <= inner <= outer < 90 degrees");
    }

    if (!number_field(L, 2, "intensity", light.intensity) || !number_field(L, 2, "range", light.range) ||
        !number_field(L, 2, "radius", light.radius)) {
        return fail(L, "intensity, range and radius are numbers");
    }
    if (light.range <= 0.0f || light.intensity < 0.0f || light.radius < 0.0f) {
        return fail(L, "range must be above 0, intensity and radius not negative");
    }
    if (!flag_field(L, 2, "shadows", light.casts_shadows) || !flag_field(L, 2, "gi", light.gi)) {
        return fail(L, "shadows and gi are true or false");
    }
    sw::carry_light(std::move(light));
    lua_pushboolean(L, 1);
    return 1;
}

int l_drop(lua_State* L) {
    const std::string id = lua_isnoneornil(L, 1) ? std::string() : std::string(luaL_checkstring(L, 1));
    lua_pushboolean(L, sw::drop_carried_lights(caller_mod(L), id));
    return 1;
}

}  // namespace

void install_light_family(lua_State* L, Host* host) {
    if (lua_getfield(L, -1, "light") != LUA_TTABLE) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, "light");
    }
    for (const auto& f : {std::pair<const char*, lua_CFunction>{"carry", l_carry}, {"drop", l_drop}}) {
        lua_pushlightuserdata(L, host);
        lua_pushcclosure(L, f.second, 1);
        lua_setfield(L, -2, f.first);
    }
    lua_pop(L, 1);
}

}  // namespace sh::script
