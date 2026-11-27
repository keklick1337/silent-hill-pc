// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// sh.world.pickup_create{item=, x=, y=, z=, rot=, id=, flag=, count=, message=, model=, units=} -> id | nil, err
// sh.world.pickup_remove(id) -> removed
// sh.world.pickups([units]) -> {{id, owner, item, model, flag, count, x, y, z, rot, taken}, ...}
// sh.world.message(key) -> true | nil, err
// Units as in the rest of the API: positions Q12 (4096 = 1 m, y negative up; no y: on the ground at x, z), rot a
// Q12 angle (4096 = a full turn): a number is the yaw, a table {x, y, z} the full rotation. The first form of these
// functions took metres and degrees (as `pos` prints them); it is still read when units = "metres" or when any
// coordinate or angle is a fractional number. The world model and the prompt default to the item's.

#include "engine/script/bind_world_pickups.h"

#include "engine/ids/game_numbers.h"
#include "engine/script/bind_flags.h"
#include "engine/script/caller_mod.h"
#include "engine/script/host_interfaces.h"
#include "engine/script/script_host.h"
#include "engine/world/runtime_pickups.h"

#include <cstring>
#include <string>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace sh::script {

namespace {

namespace sw = sh::world;

Host* host_of(lua_State* L) { return static_cast<Host*>(lua_touserdata(L, lua_upvalueindex(1))); }

std::int32_t metres_q12(double m) { return static_cast<std::int32_t>(static_cast<float>(m) * 4096.0f); }
std::int16_t degrees_q12(double d) {
    return static_cast<std::int16_t>(static_cast<std::int32_t>(static_cast<float>(d) / 360.0f * 4096.0f));
}

enum class Units { Auto, Q12, Metres };

// units = "q12" | "metres" ("meters"); nil: decided by the numbers. False for anything else.
bool read_units(lua_State* L, int t, Units& out) {
    const int type = lua_getfield(L, t, "units");
    bool ok = true;
    out = Units::Auto;
    if (type == LUA_TSTRING) {
        const std::string u = lua_tostring(L, -1);
        if (u == "q12") {
            out = Units::Q12;
        } else if (u == "metres" || u == "meters") {
            out = Units::Metres;
        } else {
            ok = false;
        }
    } else if (type != LUA_TNIL) {
        ok = false;
    }
    lua_pop(L, 1);
    return ok;
}

bool is_fraction(lua_State* L, int idx) { return lua_type(L, idx) == LUA_TNUMBER && !lua_isinteger(L, idx); }

// The first form wrote metres and degrees with fractions (5.19, -1.01, 274.0): a fractional number anywhere among
// the coordinates and angles marks the whole call as that form.
bool has_fraction(lua_State* L, int t) {
    bool found = false;
    for (const char* key : {"x", "y", "z"}) {
        lua_getfield(L, t, key);
        found = found || is_fraction(L, -1);
        lua_pop(L, 1);
    }
    if (lua_getfield(L, t, "rot") == LUA_TTABLE) {
        const char* keys[] = {"x", "y", "z"};
        for (int k = 0; k < 3; ++k) {
            lua_getfield(L, -1, keys[k]);
            found = found || is_fraction(L, -1);
            lua_pop(L, 1);
            lua_rawgeti(L, -1, k + 1);
            found = found || is_fraction(L, -1);
            lua_pop(L, 1);
        }
    } else {
        found = found || is_fraction(L, -1);
    }
    lua_pop(L, 1);
    return found;
}

bool opt_string(lua_State* L, int t, const char* key, std::string& out) {
    const int type = lua_getfield(L, t, key);
    bool ok = true;
    if (type == LUA_TSTRING) {
        out = lua_tostring(L, -1);
    } else if (type != LUA_TNIL) {
        ok = false;
    }
    lua_pop(L, 1);
    return ok;
}

// A coordinate (the value at the top of the stack): a Q12 integer, or metres. False when it is not a number, or a
// fraction where Q12 is asked for.
bool position_value(lua_State* L, Units units, std::int32_t& out) {
    if (lua_type(L, -1) != LUA_TNUMBER) {
        return false;
    }
    if (units == Units::Metres) {
        out = metres_q12(lua_tonumber(L, -1));
        return true;
    }
    if (!lua_isinteger(L, -1)) {
        return false;
    }
    out = static_cast<std::int32_t>(lua_tointeger(L, -1));
    return true;
}

// An angle: a Q12 angle, or degrees.
bool angle_value(lua_State* L, Units units, std::int16_t& out) {
    if (lua_type(L, -1) != LUA_TNUMBER) {
        return false;
    }
    if (units == Units::Metres) {
        out = degrees_q12(lua_tonumber(L, -1));
        return true;
    }
    if (!lua_isinteger(L, -1)) {
        return false;
    }
    out = static_cast<std::int16_t>(lua_tointeger(L, -1));
    return true;
}

// 0: missing, 1: read, -1: not a number (or a fraction where Q12 is asked for).
int opt_position(lua_State* L, int t, const char* key, Units units, std::int32_t& out) {
    const int type = lua_getfield(L, t, key);
    int r = 0;
    if (type != LUA_TNIL) {
        r = position_value(L, units, out) ? 1 : -1;
    }
    lua_pop(L, 1);
    return r;
}

int fail(lua_State* L, const std::string& why) {
    lua_pushnil(L);
    lua_pushstring(L, why.c_str());
    return 2;
}

int l_pickup_create(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    sw::PickupSpec spec;
    spec.owner = caller_mod(L);
    spec.origin = caller_origin(L);
    for (const auto& [key, out] : {std::pair<const char*, std::string*>{"item", &spec.item},
                                   {"id", &spec.id},
                                   {"flag", &spec.flag},
                                   {"message", &spec.message},
                                   {"model", &spec.model}}) {
        if (std::string_view(key) == "model") {
            lua_getfield(L, 1, "model");
            const bool none = lua_isboolean(L, -1) && !lua_toboolean(L, -1);
            lua_pop(L, 1);
            if (none) {
                spec.unseen = true;
                continue;
            }
        }
        if (!opt_string(L, 1, key, *out)) {
            return fail(L, std::string(key) + " must be a string");
        }
    }
    Units units = Units::Auto;
    if (!read_units(L, 1, units)) {
        return fail(L, "units must be \"q12\" or \"metres\"");
    }
    if (units == Units::Auto) {
        units = has_fraction(L, 1) ? Units::Metres : Units::Q12;
    }
    const std::string unit_name = units == Units::Metres ? "metres" : "Q12 integers";
    std::int32_t x = 0, y = 0, z = 0;
    if (opt_position(L, 1, "x", units, x) != 1 || opt_position(L, 1, "z", units, z) != 1) {
        return fail(L, "x and z are required numbers (" + unit_name + ")");
    }
    const int has_y = opt_position(L, 1, "y", units, y);
    if (has_y < 0) {
        return fail(L, "y must be a number (" + unit_name + ")");
    }
    spec.ground = has_y == 0;
    spec.position = {x, y, z};
    const std::string bad_rot =
        std::string("rot must be ") + (units == Units::Metres ? "degrees" : "a Q12 angle") + " (a number) or {x, y, z}";
    const int rot_type = lua_getfield(L, 1, "rot");
    if (rot_type == LUA_TTABLE) {
        const char* keys[] = {"x", "y", "z"};
        for (int k = 0; k < 3; ++k) {
            if (lua_getfield(L, -1, keys[k]) == LUA_TNIL) {
                lua_pop(L, 1);
                lua_rawgeti(L, -1, k + 1);
            }
            const bool ok = lua_isnil(L, -1) || angle_value(L, units, spec.rotation[static_cast<std::size_t>(k)]);
            lua_pop(L, 1);
            if (!ok) {
                lua_pop(L, 1);
                return fail(L, bad_rot);
            }
        }
    } else if (rot_type != LUA_TNIL && !angle_value(L, units, spec.rotation[1])) {
        lua_pop(L, 1);
        return fail(L, bad_rot);
    }
    lua_pop(L, 1);
    if (lua_getfield(L, 1, "count") != LUA_TNIL) {
        if (lua_type(L, -1) != LUA_TNUMBER) {
            lua_pop(L, 1);
            return fail(L, "count must be a number");
        }
        spec.count = static_cast<int>(lua_tonumber(L, -1));
    }
    lua_pop(L, 1);
    if (!spec.id.empty() && !spec.owner.empty() && spec.id.find(':') == std::string::npos) {
        spec.id = spec.owner + ":" + spec.id;  // a mod's bare id lives in its namespace
    }
    // A short item or flag name of a mod means the mod's own first.
    const sh::ids::NamespaceScope names(spec.owner);
    Result<std::string> made = sw::pickups().create(std::move(spec));
    if (!made) {
        host_of(L)->log(2, "sh.world.pickup_create: " + made.error());
        return fail(L, made.error());
    }
    lua_pushstring(L, made.value().c_str());
    return 1;
}

std::string id_arg(lua_State* L, int arg) {
    std::string id = luaL_checkstring(L, arg);
    if (id.find(':') == std::string::npos && sw::pickups().find(id) == nullptr) {
        const std::string owner = caller_mod(L);
        if (!owner.empty()) {
            id = owner + ":" + id;
        }
    }
    return id;
}

// sh.world.message(key) -> true | nil, err: the map message `key` (a message number of the map, or a text key a
// mod's text/<lang>/*.json with "map" adds to it) shows as soon as Harry is free, the world stopped, as the map's own
// message events do.
int l_message(lua_State* L) {
    int index = -1;
    if (lua_type(L, 1) == LUA_TSTRING) {
        const auto find = host_of(L)->game_api().msg_index;
        index = find != nullptr ? find(lua_tostring(L, 1)) : -1;
    } else {
        index = static_cast<int>(luaL_checkinteger(L, 1));
    }
    if (index < 0 || index > 255) {
        lua_pushnil(L);
        lua_pushfstring(L, "no message '%s' on this map", luaL_tolstring(L, 1, nullptr));
        return 2;
    }
    sw::request_script_message(index);
    lua_pushboolean(L, 1);
    return 1;
}

// sh.world.say(key) -> true | nil, err: the map message `key` shown over play, the world going on; the confirm
// button advances it. sh.world.saying() -> bool: it still shows.
int l_say(lua_State* L) {
    int index = -1;
    if (lua_type(L, 1) == LUA_TSTRING) {
        const auto find = host_of(L)->game_api().msg_index;
        index = find != nullptr ? find(lua_tostring(L, 1)) : -1;
    } else {
        index = static_cast<int>(luaL_checkinteger(L, 1));
    }
    if (index < 0 || index > 255) {
        lua_pushnil(L);
        lua_pushfstring(L, "no message '%s' on this map", luaL_tolstring(L, 1, nullptr));
        return 2;
    }
    sw::request_soft_message(index);
    lua_pushboolean(L, 1);
    return 1;
}

int l_saying(lua_State* L) {
    lua_pushboolean(L, sw::soft_message() >= 0);
    return 1;
}

// sh.world.hush(): the message of sh.world.say ends now (a script skips it on a button).
int l_hush(lua_State*) {
    sw::clear_soft_message();
    return 0;
}

// sh.world.pickup_take(id) -> bool: the pickup's prompt starts as if the player had taken it up (once nothing else
// runs); false when no such pickup lies on the map.
int l_pickup_take(lua_State* L) {
    const std::string id = id_arg(L, 1);
    const sw::Pickup* p = sw::pickups().find(id);
    const bool ok = p != nullptr;
    if (ok) sw::request_pickup_take(id);
    lua_pushboolean(L, ok);
    return 1;
}

// sh.world.mark(id, {x, z, r, label, kind}) / sh.world.unmark([id]): a place of the script's story the modding tools'
// overlay draws (x, z, r in metres); gone at a map change.
int l_mark(lua_State* L) {
    sw::ScriptMark mark;
    mark.owner = caller_mod(L);
    mark.origin = caller_origin(L);
    mark.id = id_arg(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    auto num = [&](const char* k) {
        lua_getfield(L, 2, k);
        const double v = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 0.0;
        lua_pop(L, 1);
        return static_cast<std::int32_t>(v * 4096.0);  // metres
    };
    auto str = [&](const char* k, std::string& out) {
        lua_getfield(L, 2, k);
        if (lua_isstring(L, -1)) out = lua_tostring(L, -1);
        lua_pop(L, 1);
    };
    mark.x = num("x");
    mark.z = num("z");
    mark.radius = num("r");
    str("label", mark.label);
    str("kind", mark.kind);
    if (mark.label.empty()) mark.label = mark.id;
    sw::set_script_mark(std::move(mark));
    return 0;
}

int l_unmark(lua_State* L) {
    const std::string id = lua_isnoneornil(L, 1) ? std::string() : id_arg(L, 1);
    lua_pushboolean(L, sw::clear_script_marks(caller_mod(L), id));
    return 1;
}

int l_pickup_remove(lua_State* L) {
    lua_pushboolean(L, sw::pickups().remove(id_arg(L, 1)));
    return 1;
}

bool flag_is_set(lua_State* L, int flag) {
    if (FlagsHost* f = flags_host(); f != nullptr) {
        return f->get(flag) != 0;
    }
    const auto get = host_of(L)->game_api().flag_get;
    return get != nullptr && get(flag) != 0;
}

int l_pickups(lua_State* L) {
    bool metres = false;
    if (!lua_isnoneornil(L, 1)) {
        const std::string u = luaL_checkstring(L, 1);
        if (u == "metres" || u == "meters") {
            metres = true;
        } else if (u != "q12") {
            return luaL_argerror(L, 1, "units must be \"q12\" or \"metres\"");
        }
    }
    const auto& all = sw::pickups().all();
    lua_createtable(L, static_cast<int>(all.size()), 0);
    int n = 0;
    for (const sw::Pickup& p : all) {
        lua_createtable(L, 0, 12);
        const auto str = [L](const char* key, const std::string& v) {
            lua_pushstring(L, v.c_str());
            lua_setfield(L, -2, key);
        };
        const auto position = [L, metres](const char* key, std::int32_t v) {
            if (metres) {
                lua_pushnumber(L, v / 4096.0);
            } else {
                lua_pushinteger(L, v);
            }
            lua_setfield(L, -2, key);
        };
        str("id", p.spec.id);
        str("owner", p.spec.owner);
        str("item", p.spec.item);
        str("model", p.spec.model);
        str("flag", p.spec.flag);
        if (!p.spec.message.empty()) {
            str("message", p.spec.message);
        }
        lua_pushinteger(L, p.spec.count);
        lua_setfield(L, -2, "count");
        position("x", p.spec.position[0]);
        position("y", p.spec.position[1]);
        position("z", p.spec.position[2]);
        if (metres) {
            lua_pushnumber(L, p.spec.rotation[1] * 360.0 / 4096.0);
        } else {
            lua_pushinteger(L, p.spec.rotation[1]);
        }
        lua_setfield(L, -2, "rot");
        lua_pushboolean(L, p.resolved.flag >= 0 && flag_is_set(L, p.resolved.flag));
        lua_setfield(L, -2, "taken");
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

}  // namespace

void install_world_pickup_family(lua_State* L, Host* host) {
    if (lua_getfield(L, -1, "world") != LUA_TTABLE) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, "world");
    }
    for (const auto& f : {std::pair<const char*, lua_CFunction>{"pickup_create", l_pickup_create},
                          {"pickup_remove", l_pickup_remove},
                          {"pickup_take", l_pickup_take},
                          {"mark", l_mark},
                          {"unmark", l_unmark},
                          {"pickups", l_pickups},
                          {"message", l_message},
                          {"say", l_say},
                          {"saying", l_saying},
                          {"hush", l_hush}}) {
        lua_pushlightuserdata(L, host);
        lua_pushcclosure(L, f.second, 1);
        lua_setfield(L, -2, f.first);
    }
    lua_pop(L, 1);
}

}  // namespace sh::script
