// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Lua <-> sh::menu::Value conversions shared by sh.menu and sh.config. None raises a Lua error.
#pragma once

#include "engine/menu/menu_model.h"

#include <string>
#include <vector>

extern "C" {
#include "lua.h"
}

namespace sh::script::lua_values {

using sh::menu::Choice;
using sh::menu::Value;

inline Value to_value(lua_State* L, int idx) {
    switch (lua_type(L, idx)) {
        case LUA_TBOOLEAN: return Value{lua_toboolean(L, idx) != 0};
        case LUA_TNUMBER:
            if (lua_isinteger(L, idx)) {
                return Value{static_cast<std::int64_t>(lua_tointeger(L, idx))};
            }
            return Value{static_cast<double>(lua_tonumber(L, idx))};
        case LUA_TSTRING: return Value{std::string(lua_tostring(L, idx))};
        default: return Value{};
    }
}

inline void push_value(lua_State* L, const Value& v) {
    if (const auto* b = std::get_if<bool>(&v)) {
        lua_pushboolean(L, *b ? 1 : 0);
    } else if (const auto* i = std::get_if<std::int64_t>(&v)) {
        lua_pushinteger(L, static_cast<lua_Integer>(*i));
    } else if (const auto* d = std::get_if<double>(&v)) {
        lua_pushnumber(L, *d);
    } else if (const auto* s = std::get_if<std::string>(&v)) {
        lua_pushlstring(L, s->data(), s->size());
    } else {
        lua_pushnil(L);
    }
}

// A string, number or boolean as text; false for anything else.
inline bool scalar_text(lua_State* L, int idx, std::string& out) {
    switch (lua_type(L, idx)) {
        case LUA_TSTRING:
        case LUA_TNUMBER:
            lua_pushvalue(L, idx);  // lua_tolstring converts a number in place
            out = lua_tostring(L, -1);
            lua_pop(L, 1);
            return true;
        case LUA_TBOOLEAN: out = lua_toboolean(L, idx) ? "true" : "false"; return true;
        default: return false;
    }
}

// Reads the {value=, label=} / plain-string list at `idx`; entries that make no sense are skipped.
inline std::vector<Choice> read_choices(lua_State* L, int idx) {
    std::vector<Choice> out;
    if (lua_type(L, idx) != LUA_TTABLE) {
        return out;
    }
    idx = lua_absindex(L, idx);
    const lua_Unsigned n = lua_rawlen(L, idx);
    for (lua_Unsigned i = 1; i <= n; ++i) {
        lua_rawgeti(L, idx, static_cast<lua_Integer>(i));
        Choice c;
        if (lua_type(L, -1) == LUA_TTABLE) {
            lua_pushliteral(L, "value");  // raw gets: a result table's metamethods must not raise here
            lua_rawget(L, -2);
            const bool has_value = scalar_text(L, -1, c.value);
            lua_pop(L, 1);
            lua_pushliteral(L, "label");
            lua_rawget(L, -2);
            const bool has_label = scalar_text(L, -1, c.label);
            lua_pop(L, 1);
            if (!has_value && !has_label) {
                lua_pop(L, 1);
                continue;
            }
            if (!has_label) {
                c.label = c.value;
            } else if (!has_value) {
                c.value = c.label;
            }
            out.push_back(std::move(c));
        } else if (scalar_text(L, -1, c.value)) {
            c.label = c.value;
            out.push_back(std::move(c));
        }
        lua_pop(L, 1);
    }
    return out;
}

}  // namespace sh::script::lua_values
