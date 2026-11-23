// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/script/bind_config.h"

#include "engine/script/lua_values.h"
#include "engine/script/mod_settings.h"
#include "engine/script/script_host.h"

#include <cstdio>
#include <new>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace sh::script {

namespace {

using lua_values::push_value;
using lua_values::read_choices;
using lua_values::to_value;
using sh::menu::Value;

ConfigScope* scope_of(lua_State* L) { return static_cast<ConfigScope*>(lua_touserdata(L, lua_upvalueindex(1))); }

void say(lua_State* L, const std::string& m) {
    ConfigScope* s = scope_of(L);
    if (s != nullptr && s->log) {
        s->log(1, "sh.config: " + m);
    } else {
        std::fprintf(stderr, "[config] %s\n", m.c_str());
    }
}

// A failure is logged and returned as false, message: a mod's typo must not stop its file.
int fail(lua_State* L, const std::string& m) {
    say(L, m);
    lua_pushboolean(L, 0);
    lua_pushlstring(L, m.data(), m.size());
    return 2;
}

int ok(lua_State* L) {
    lua_pushboolean(L, 1);
    return 1;
}

bool opt_number(lua_State* L, int t, const char* key, double& out, bool& wrong) {
    lua_getfield(L, t, key);
    const int ty = lua_type(L, -1);
    if (ty == LUA_TNUMBER) out = lua_tonumber(L, -1);
    else if (ty != LUA_TNIL) wrong = true;
    lua_pop(L, 1);
    return ty == LUA_TNUMBER;
}

int l_declare(lua_State* L) {
    if (lua_type(L, 1) != LUA_TTABLE) return fail(L, "declare expects a table");
    std::string err;
    {
        SettingDecl d;
        lua_getfield(L, 1, "key");
        if (lua_type(L, -1) == LUA_TSTRING) d.key = lua_tostring(L, -1);
        lua_pop(L, 1);
        std::string type = "number";
        lua_getfield(L, 1, "type");
        if (lua_type(L, -1) == LUA_TSTRING) type = lua_tostring(L, -1);
        lua_pop(L, 1);
        if (type == "number") d.type = SettingType::Number;
        else if (type == "bool" || type == "boolean") d.type = SettingType::Bool;
        else if (type == "choice") d.type = SettingType::Choice;
        else if (type == "string") d.type = SettingType::String;
        else err = d.key + ": unknown type '" + type + "' (number, bool, choice, string)";

        bool wrong = false;
        opt_number(L, 1, "min", d.min, wrong);
        opt_number(L, 1, "max", d.max, wrong);
        opt_number(L, 1, "step", d.step, wrong);
        lua_getfield(L, 1, "default");
        d.def = to_value(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 1, "choices");
        d.choices = read_choices(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, 1, "restart");
        d.restart = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
        if (err.empty() && wrong) err = d.key + ": min, max and step must be numbers";
        if (err.empty()) mod_settings().declare(std::move(d), err);
    }
    return err.empty() ? ok(L) : fail(L, err);
}

int l_get(lua_State* L) {
    const char* key = lua_tostring(L, 1);
    const auto v = key ? mod_settings().get(key) : std::nullopt;
    if (v) push_value(L, *v);
    else lua_pushnil(L);
    return 1;
}

int l_default(lua_State* L) {
    const char* key = lua_tostring(L, 1);
    const SettingDecl* d = key ? mod_settings().find(key) : nullptr;
    if (d) push_value(L, d->def);
    else lua_pushnil(L);
    return 1;
}

int l_set(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) return fail(L, "set(key, value) expects a key string");
    std::string err;
    {
        const std::string key = lua_tostring(L, 1);
        mod_settings().set(key, to_value(L, 2), err);
    }
    return err.empty() ? ok(L) : fail(L, err);
}

int l_reset(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) return fail(L, "reset(key) expects a key string");
    std::string err;
    {
        const std::string key = lua_tostring(L, 1);
        mod_settings().reset(key, err);
    }
    return err.empty() ? ok(L) : fail(L, err);
}

int l_on_change(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING || lua_type(L, 2) != LUA_TFUNCTION) {
        return fail(L, "on_change(key, fn) expects a key string and a function");
    }
    ConfigScope* s = scope_of(L);
    std::string err;
    {
        const std::string key = lua_tostring(L, 1);
        if (s == nullptr || !s->listener) {
            err = "on_change is available in menu scripts only";
        } else if (mod_settings().find(key) == nullptr) {
            err = "'" + key + "' is not a declared setting";
        } else {
            mod_settings().on_change(key, s->listener(L, 2), err);
        }
    }
    return err.empty() ? ok(L) : fail(L, err);
}

int l_needs_restart(lua_State* L) {
    lua_pushboolean(L, mod_settings().needs_restart() ? 1 : 0);
    return 1;
}

void push_functions(lua_State* L, int scope_idx) {
    static const luaL_Reg fns[] = {{"declare", l_declare},     {"get", l_get},
                                   {"set", l_set},             {"default", l_default},
                                   {"reset", l_reset},         {"on_change", l_on_change},
                                   {"needs_restart", l_needs_restart}, {nullptr, nullptr}};
    lua_newtable(L);
    for (const luaL_Reg* f = fns; f->name != nullptr; ++f) {
        lua_pushvalue(L, scope_idx);
        lua_pushcclosure(L, f->func, 1);
        lua_setfield(L, -2, f->name);
    }
}

int scope_gc(lua_State* L) {
    static_cast<ConfigScope*>(lua_touserdata(L, 1))->~ConfigScope();
    return 0;
}

}  // namespace

void install_config_table(lua_State* L, ConfigScope* scope) {
    lua_pushlightuserdata(L, scope);
    const int idx = lua_gettop(L);
    push_functions(L, idx);
    lua_setfield(L, -3, "config");
    lua_pop(L, 1);
}

void install_config_family(lua_State* L, Host* host) {
    // The scope lives in the state itself (a userdata with __gc), so it goes when the host does.
    void* mem = lua_newuserdatauv(L, sizeof(ConfigScope), 0);
    auto* scope = new (mem) ConfigScope{};
    scope->log = [host](int level, const std::string& m) { host->log(level, m); };
    lua_createtable(L, 0, 1);
    lua_pushcfunction(L, scope_gc);
    lua_setfield(L, -2, "__gc");
    lua_setmetatable(L, -2);
    const int idx = lua_gettop(L);
    push_functions(L, idx);
    lua_setfield(L, -3, "config");
    lua_pop(L, 1);
}

}  // namespace sh::script
