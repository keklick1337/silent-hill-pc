// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/script/bind_mods.h"

#include "engine/mods/mod_import.h"
#include "engine/mods/mods_admin.h"

#include <filesystem>
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

// ---- importing mods (engine/mods/mod_import.h)

sh::mods::PickKind pick_kind(lua_State* L, int idx) {
    const std::string kind = luaL_checkstring(L, idx);
    if (kind == "folder") return sh::mods::PickKind::Folder;
    if (kind == "zip") return sh::mods::PickKind::Zip;
    luaL_argerror(L, idx, "\"folder\" or \"zip\"");
    return sh::mods::PickKind::Zip;
}

int l_import_can(lua_State* L) {
    lua_pushboolean(L, sh::mods::import_session().can_pick(pick_kind(L, 1)) ? 1 : 0);
    return 1;
}

int l_import_pick(lua_State* L) {
    lua_pushboolean(L, sh::mods::import_session().pick(pick_kind(L, 1)) ? 1 : 0);
    return 1;
}

int l_import_poll(lua_State* L) {
    auto& s = sh::mods::import_session();
    const bool changed = s.poll();
    lua_pushstring(L, sh::mods::import_state_name(s.state()));
    lua_pushboolean(L, changed ? 1 : 0);
    return 2;
}

int l_import_scan(lua_State* L) {
    auto& s = sh::mods::import_session();
    const std::string path = luaL_checkstring(L, 1);
    s.scan(std::filesystem::path(std::u8string(path.begin(), path.end())));
    lua_pushstring(L, sh::mods::import_state_name(s.state()));
    return 1;
}

int l_import_found(lua_State* L) {
    const sh::mods::ImportScan& scan = sh::mods::import_session().result();
    lua_newtable(L);
    const std::u8string source = scan.source.u8string();
    set_string(L, "source", std::string(source.begin(), source.end()));
    set_bool(L, "zip", scan.zip);
    set_string(L, "error", sh::mods::import_error_name(scan.error));
    set_string(L, "detail", scan.detail);
    lua_createtable(L, static_cast<int>(scan.mods.size()), 0);
    for (std::size_t i = 0; i < scan.mods.size(); ++i) {
        const sh::mods::FoundMod& m = scan.mods[i];
        lua_newtable(L);
        set_int(L, "index", static_cast<lua_Integer>(i + 1));
        set_string(L, "location", m.location);
        set_string(L, "id", m.id);
        set_string(L, "name", m.name.empty() ? (m.location.empty() ? std::string("?") : m.location) : m.name);
        set_string(L, "version", m.version);
        set_string(L, "status", sh::mods::found_status_name(m.status));
        set_string(L, "reason", sh::mods::invalid_reason_name(m.reason));
        set_string(L, "detail", m.detail);
        set_string(L, "installed_version", m.installed_version);
        set_int(L, "files", static_cast<lua_Integer>(m.files));
        set_int(L, "bytes", static_cast<lua_Integer>(m.bytes));
        set_bool(L, "selected", m.selected);
        lua_createtable(L, static_cast<int>(m.notes.size()), 0);
        for (std::size_t n = 0; n < m.notes.size(); ++n) {
            lua_newtable(L);
            set_string(L, "kind", m.notes[n].kind);
            set_string(L, "detail", m.notes[n].detail);
            set_string(L, "need", m.notes[n].need);
            set_string(L, "have", m.notes[n].have);
            lua_rawseti(L, -2, static_cast<lua_Integer>(n + 1));
        }
        lua_setfield(L, -2, "notes");
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    lua_setfield(L, -2, "mods");
    return 1;
}

int l_import_select(lua_State* L) {
    const lua_Integer index = luaL_checkinteger(L, 1);
    const bool on = lua_toboolean(L, 2) != 0;
    const bool done = index >= 1 && sh::mods::import_session().select(static_cast<std::size_t>(index - 1), on);
    lua_pushboolean(L, done ? 1 : 0);
    return 1;
}

void push_report(lua_State* L, const sh::mods::ImportReport& report) {
    lua_newtable(L);
    set_list(L, "added", report.added);
    set_list(L, "replaced", report.replaced);
}

int l_import_run(lua_State* L) {
    const Result<sh::mods::ImportReport> done = sh::mods::import_session().run(sh::mods::admin_registry());
    lua_pushboolean(L, done ? 1 : 0);
    if (done) {
        push_report(L, done.value());
    } else {
        lua_pushlstring(L, done.error().data(), done.error().size());
    }
    return 2;
}

int l_import_last(lua_State* L) {
    const auto& s = sh::mods::import_session();
    if (s.last_report()) {
        push_report(L, *s.last_report());
    } else {
        lua_pushnil(L);
    }
    lua_pushlstring(L, s.last_error().data(), s.last_error().size());
    return 2;
}

int l_import_clear(lua_State*) {
    sh::mods::import_session().clear();
    return 0;
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
                                   {"import_can", l_import_can},
                                   {"import_pick", l_import_pick},
                                   {"import_poll", l_import_poll},
                                   {"import_scan", l_import_scan},
                                   {"import_found", l_import_found},
                                   {"import_select", l_import_select},
                                   {"import_run", l_import_run},
                                   {"import_last", l_import_last},
                                   {"import_clear", l_import_clear},
                                   {nullptr, nullptr}};
    luaL_newlib(L, fns);
    lua_setfield(L, -2, "mods");
}

}  // namespace sh::script
