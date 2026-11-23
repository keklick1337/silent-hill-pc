// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/script/bind_mods.h"

#include "engine/mods/mods_admin.h"

#include <string>
#include <vector>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace sh::script {

namespace {

using sh::mods::ModState;
using sh::mods::Registry;
using sh::mods::ResolvedMod;

void set_string(lua_State* L, const char* key, const std::string& value) {
    lua_pushlstring(L, value.data(), value.size());
    lua_setfield(L, -2, key);
}

void set_int(lua_State* L, const char* key, lua_Integer value) {
    lua_pushinteger(L, value);
    lua_setfield(L, -2, key);
}

void set_bool(lua_State* L, const char* key, bool value) {
    lua_pushboolean(L, value ? 1 : 0);
    lua_setfield(L, -2, key);
}

void set_list(lua_State* L, const char* key, const std::vector<std::string>& values) {
    lua_createtable(L, static_cast<int>(values.size()), 0);
    for (std::size_t i = 0; i < values.size(); ++i) {
        lua_pushlstring(L, values[i].data(), values[i].size());
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    lua_setfield(L, -2, key);
}

void push_mod(lua_State* L, const Registry& reg, const ResolvedMod& m) {
    const auto& man = m.candidate.manifest;
    lua_newtable(L);
    set_string(L, "id", man.id);
    set_string(L, "name", man.name.empty() ? man.id : man.name);
    set_string(L, "version", man.version_text.empty() ? man.version.to_string() : man.version_text);
    set_string(L, "description", man.description);
    set_list(L, "authors", man.authors);
    set_string(L, "homepage", man.homepage);
    set_string(L, "state", sh::mods::state_name(m.state));
    const sh::mods::UserEntry* choice = reg.pending_user_state().find(man.id);
    // An error is the engine's verdict, not the player's: the switch still shows what the player chose.
    set_bool(L, "enabled", choice != nullptr && choice->enabled);
    const ResolvedMod* running = reg.plan().find(man.id);
    const bool loaded = running != nullptr && running->state == ModState::Active;
    set_int(L, "load_index", loaded ? running->load_index + 1 : 0);
    set_bool(L, "running", loaded);
    set_list(L, "problems", m.problems);
    set_list(L, "warnings", m.warnings);
    set_bool(L, "gameplay", man.gameplay);
    set_bool(L, "scripts", man.scripts);
    set_bool(L, "requires_restart", man.requires_restart);
    lua_Integer replaced = 0;
    lua_Integer added = 0;
    lua_Integer overridden = 0;
    if (loaded) {
        for (const sh::mods::ModFile& f : reg.files(man.id)) {
            (f.replaces ? replaced : added)++;
            overridden += f.wins ? 0 : 1;
        }
    }
    set_int(L, "files_replaced", replaced);
    set_int(L, "files_added", added);
    set_int(L, "files_overridden", overridden);
}

int l_list(lua_State* L) {
    const Registry& reg = sh::mods::admin_registry();
    const auto& mods = reg.pending().mods;
    lua_createtable(L, static_cast<int>(mods.size()), 0);
    for (std::size_t i = 0; i < mods.size(); ++i) {
        push_mod(L, reg, mods[i]);
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

int l_set_enabled(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    const bool on = lua_toboolean(L, 2) != 0;
    lua_pushboolean(L, sh::mods::admin_registry().set_enabled(id, on) ? 1 : 0);
    return 1;
}

int l_move(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    const lua_Integer delta = luaL_checkinteger(L, 2);
    const int d = delta < -1000 ? -1000 : delta > 1000 ? 1000 : static_cast<int>(delta);
    lua_pushboolean(L, sh::mods::admin_registry().move(id, d) ? 1 : 0);
    return 1;
}

int l_save(lua_State* L) {
    const Result<void> saved = sh::mods::admin_registry().save();
    lua_pushboolean(L, saved ? 1 : 0);
    if (saved) {
        return 1;
    }
    lua_pushlstring(L, saved.error().data(), saved.error().size());
    return 2;
}

int l_restart_pending(lua_State* L) {
    lua_pushboolean(L, sh::mods::admin_registry().restart_pending() ? 1 : 0);
    return 1;
}

int l_apply_live(lua_State* L) {
    lua_pushboolean(L, sh::mods::admin_registry().apply_live() ? 1 : 0);
    return 1;
}

int l_editable(lua_State* L) {
    lua_pushboolean(L, sh::mods::editable() ? 1 : 0);
    return 1;
}

int l_restart(lua_State* L) {
    // Returns only on failure: a successful restart replaces the process.
    const Result<void> done = sh::mods::request_restart();
    lua_pushboolean(L, done ? 1 : 0);
    if (done) {
        return 1;
    }
    lua_pushlstring(L, done.error().data(), done.error().size());
    return 2;
}

int l_reload(lua_State* L) {
    const std::vector<std::string> lines = sh::mods::hot_reload(sh::mods::admin_registry());
    lua_createtable(L, static_cast<int>(lines.size()), 0);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        lua_pushlstring(L, lines[i].data(), lines[i].size());
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

int l_dir(lua_State* L) {
    const auto& dirs = sh::mods::admin_registry().config().dirs;
    const std::string dir = dirs.empty() ? std::string() : dirs.front().generic_string();
    lua_pushlstring(L, dir.data(), dir.size());
    return 1;
}

}  // namespace

void open_mods_api(lua_State* L) {
    static const luaL_Reg fns[] = {{"list", l_list},
                                   {"set_enabled", l_set_enabled},
                                   {"move", l_move},
                                   {"save", l_save},
                                   {"restart_pending", l_restart_pending},
                                   {"apply_live", l_apply_live},
                                   {"editable", l_editable},
                                   {"restart", l_restart},
                                   {"reload", l_reload},
                                   {"dir", l_dir},
                                   {nullptr, nullptr}};
    luaL_newlib(L, fns);
    lua_setfield(L, -2, "mods");
}

}  // namespace sh::script
