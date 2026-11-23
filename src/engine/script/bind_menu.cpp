// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/script/bind_menu.h"

#include "engine/menu/menu_model.h"
#include "engine/mods/mods.h"
#include "engine/script/bind_engine.h"
#include "engine/script/bind_mods.h"
#include "engine/script/bind_config.h"
#include "engine/script/lua_values.h"
#include "engine/script/mod_settings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

namespace sh::script {

namespace {

namespace fs = std::filesystem;
using sh::menu::Choice;
using sh::menu::MenuDef;
using sh::menu::RowDef;
using sh::menu::RowKind;
using sh::menu::Value;
using lua_values::push_value;
using lua_values::read_choices;
using lua_values::scalar_text;
using lua_values::to_value;

// One callback (or one chunk) may run this many VM instructions; the hook counts in steps of 1000.
constexpr unsigned kInstructionBudget = 2'000'000;
constexpr int kHookStep = 1000;

constexpr std::string_view kOptionsMenu = "options";
constexpr std::string_view kModOptionsMenu = "options.mod_options";

struct MenuState {
    lua_State* L = nullptr;
    sh::menu::Registry* registry = nullptr;
    MenuLogFn log;
    unsigned used = 0;
    int depth = 0;
    std::set<int> logged_refs;  // a failing callback is reported once, not every frame
    std::vector<int> after_load;  // sh.menu.after_load functions, run once every file has run
    ConfigScope config;         // sh.config of this state

    void say(int level, const std::string& m) const {
        if (log) {
            log(level, m);
        } else {
            std::fprintf(stderr, "[menu] %s\n", m.c_str());
        }
    }
};

// Every state ever created: the callbacks registered in the Registry point into them, so they live
// until the process ends.
std::vector<std::unique_ptr<MenuState>>& all_states() {
    static std::vector<std::unique_ptr<MenuState>> states;
    return states;
}

MenuState* state_of(lua_State* L) { return *static_cast<MenuState**>(lua_getextraspace(L)); }

void budget_hook(lua_State* L, lua_Debug*) {
    MenuState* st = state_of(L);
    st->used += kHookStep;
    if (st->used > kInstructionBudget) {
        luaL_error(L, "menu script exceeded its instruction budget");
    }
}

// pcall with the budget. Errors are logged (once per `ref`, 0 = always) and reported as false with
// the stack restored to what it was before the call.
bool protected_call(MenuState& st, int nargs, int nres, int ref, const char* what) {
    lua_State* L = st.L;
    const int base = lua_gettop(L) - nargs - 1;
    if (st.depth == 0) {
        st.used = 0;
    }
    ++st.depth;
    const int rc = lua_pcall(L, nargs, nres, 0);
    --st.depth;
    if (rc == LUA_OK) {
        return true;
    }
    const char* msg = lua_tostring(L, -1);
    if (ref == 0 || st.logged_refs.insert(ref).second) {
        st.say(1, std::string(what) + ": " + (msg ? msg : "error"));
    }
    lua_settop(L, base);
    return false;
}

// ---- callbacks kept alive through the registry ----

struct Fn {
    MenuState* st = nullptr;
    int ref = LUA_NOREF;
    explicit operator bool() const { return st != nullptr && ref != LUA_NOREF; }
};

Fn make_fn(lua_State* L, int idx) {
    lua_pushvalue(L, idx);
    return Fn{state_of(L), luaL_ref(L, LUA_REGISTRYINDEX)};
}

bool begin_call(const Fn& f) {
    if (!f) {
        return false;
    }
    lua_rawgeti(f.st->L, LUA_REGISTRYINDEX, f.ref);
    return true;
}

// Calls with no arguments and one result left on the stack for `use`; false on error.
template <class Use>
bool call0(const Fn& f, const char* what, Use&& use) {
    if (!begin_call(f)) {
        return false;
    }
    lua_State* L = f.st->L;
    if (!protected_call(*f.st, 0, 1, f.ref, what)) {
        return false;
    }
    use(L);
    lua_pop(L, 1);
    return true;
}

std::function<Value()> value_getter(Fn f) {
    return [f]() {
        Value v;
        call0(f, "get", [&](lua_State* L) { v = to_value(L, -1); });
        return v;
    };
}

std::function<void(const Value&)> value_setter(Fn f) {
    return [f](const Value& v) {
        if (!begin_call(f)) {
            return;
        }
        push_value(f.st->L, v);
        protected_call(*f.st, 1, 0, f.ref, "set");
    };
}

std::function<void()> action_fn(Fn f) {
    return [f]() {
        if (!begin_call(f)) {
            return;
        }
        protected_call(*f.st, 0, 0, f.ref, "action");
    };
}

std::function<std::vector<Choice>()> list_fn(Fn f) {
    return [f]() {
        std::vector<Choice> out;
        call0(f, "list", [&](lua_State* L) { out = read_choices(L, -1); });
        return out;
    };
}

std::function<std::string()> text_fn(Fn f) {
    return [f]() {
        std::string out;
        call0(f, "text", [&](lua_State* L) { scalar_text(L, -1, out); });
        return out;
    };
}

// visible / enabled: a failing or non-boolean-returning callback leaves the row visible / enabled.
std::function<bool()> bool_fn(Fn f, const char* what) {
    return [f, what]() {
        bool r = true;
        call0(f, what, [&](lua_State* L) { r = lua_toboolean(L, -1) != 0; });
        return r;
    };
}

std::function<void(int)> step_fn(Fn f) {
    return [f](int dir) {
        if (!begin_call(f)) {
            return;
        }
        lua_pushinteger(f.st->L, dir);
        protected_call(*f.st, 1, 0, f.ref, "step");
    };
}

std::function<void()> on_open_fn(Fn f) { return action_fn(f); }

// ---- parsing tables into defs; errors are returned as text, raised by the caller afterwards ----

bool kind_from(const std::string& s, RowKind& k) {
    static const struct {
        const char* name;
        RowKind kind;
    } kinds[] = {{"action", RowKind::Action},   {"toggle", RowKind::Toggle},   {"slider", RowKind::Slider},
                 {"number", RowKind::Number},   {"choice", RowKind::Choice},   {"dropdown", RowKind::Dropdown},
                 {"submenu", RowKind::Submenu}, {"setting", RowKind::Setting}, {"info", RowKind::Info},
                 {"header", RowKind::Header}};
    for (const auto& e : kinds) {
        if (s == e.name) {
            k = e.kind;
            return true;
        }
    }
    return false;
}

bool field_text(lua_State* L, int t, const char* key, std::string& out) {
    lua_getfield(L, t, key);
    const bool ok = scalar_text(L, -1, out);
    lua_pop(L, 1);
    return ok;
}

bool field_number(lua_State* L, int t, const char* key, double& out) {
    lua_getfield(L, t, key);
    const bool ok = lua_type(L, -1) == LUA_TNUMBER;
    if (ok) {
        out = lua_tonumber(L, -1);
    }
    lua_pop(L, 1);
    return ok;
}

// A function field: its Fn, or an empty one when the field is absent. `wrong` is set when it is
// present but of another type.
Fn field_fn(lua_State* L, int t, const char* key, bool& wrong) {
    Fn f;
    lua_getfield(L, t, key);
    const int ty = lua_type(L, -1);
    if (ty == LUA_TFUNCTION) {
        f = make_fn(L, -1);
    } else if (ty != LUA_TNIL) {
        wrong = true;
    }
    lua_pop(L, 1);
    return f;
}

// on_open, leave_confirm and on_leave of the menu table at `t`; false when one is not a function.
bool menu_hooks(lua_State* L, int t, MenuDef& def) {
    bool wrong = false;
    if (Fn f = field_fn(L, t, "on_open", wrong)) def.on_open = on_open_fn(f);
    if (Fn f = field_fn(L, t, "leave_confirm", wrong)) def.leave_confirm = text_fn(f);
    if (Fn f = field_fn(L, t, "on_leave", wrong)) {
        def.on_leave = [set = value_setter(f)](bool yes) { set(Value{yes}); };
    }
    return !wrong;
}

// A list of values, or one value standing for a list of one.
std::vector<Value> value_list(lua_State* L, int idx) {
    std::vector<Value> out;
    idx = lua_absindex(L, idx);
    if (lua_type(L, idx) == LUA_TTABLE) {
        const lua_Unsigned n = lua_rawlen(L, idx);
        for (lua_Unsigned i = 1; i <= n; ++i) {
            lua_rawgeti(L, idx, static_cast<lua_Integer>(i));
            out.push_back(to_value(L, -1));
            lua_pop(L, 1);
        }
    } else if (lua_type(L, idx) != LUA_TNIL) {
        out.push_back(to_value(L, idx));
    }
    return out;
}

bool parse_condition(lua_State* L, int t, sh::menu::VisibleIf& c) {
    if (lua_type(L, t) != LUA_TTABLE || !field_text(L, t, "key", c.key) || c.key.empty()) return false;
    lua_getfield(L, t, "any");
    c.any = value_list(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, t, "is");
    for (Value& v : value_list(L, -1)) c.any.push_back(std::move(v));
    lua_pop(L, 1);
    lua_getfield(L, t, "none");
    c.none = value_list(L, -1);
    lua_pop(L, 1);
    return true;
}

// visible_if = { key = ..., any = ... } or a list of such conditions (all must hold); nil is none.
bool parse_visible_if(lua_State* L, int idx, std::vector<sh::menu::VisibleIf>& out) {
    idx = lua_absindex(L, idx);
    if (lua_type(L, idx) == LUA_TNIL) return true;
    if (lua_type(L, idx) != LUA_TTABLE) return false;
    lua_getfield(L, idx, "key");
    const bool single = lua_type(L, -1) != LUA_TNIL;
    lua_pop(L, 1);
    if (single) {
        sh::menu::VisibleIf c;
        if (!parse_condition(L, idx, c)) return false;
        out.push_back(std::move(c));
        return true;
    }
    const lua_Unsigned n = lua_rawlen(L, idx);
    for (lua_Unsigned i = 1; i <= n; ++i) {
        lua_rawgeti(L, idx, static_cast<lua_Integer>(i));
        sh::menu::VisibleIf c;
        const bool ok = parse_condition(L, -1, c);
        lua_pop(L, 1);
        if (!ok) return false;
        out.push_back(std::move(c));
    }
    return n > 0;
}

bool parse_row(lua_State* L, int t, RowDef& row, std::string& err) {
    t = lua_absindex(L, t);
    if (lua_type(L, t) != LUA_TTABLE) {
        err = "a row must be a table";
        return false;
    }
    std::string kind = "action";
    field_text(L, t, "kind", kind);
    if (!kind_from(kind, row.kind)) {
        err = "unknown row kind '" + kind + "'";
        return false;
    }
    field_text(L, t, "id", row.id);
    field_text(L, t, "label", row.label);
    field_text(L, t, "help", row.help);
    field_text(L, t, "submenu", row.submenu);
    field_text(L, t, "native", row.native);
    field_text(L, t, "confirm", row.confirm);
    field_text(L, t, "config", row.config);
    // A config row takes the range its row leaves out from the declaration (NaN = not given).
    if (!field_number(L, t, "min", row.min) && !row.config.empty()) row.min = std::nan("");
    if (!field_number(L, t, "max", row.max) && !row.config.empty()) row.max = std::nan("");
    if (!row.config.empty()) row.step = std::nan("");
    lua_getfield(L, t, "integer");
    row.integer = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, t, "percent");
    row.percent = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, t, "close");
    row.close_on_activate = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    lua_getfield(L, t, "choices");
    row.choices = read_choices(L, -1);
    lua_pop(L, 1);
    if (row.kind == RowKind::Submenu && row.submenu.empty()) {
        err = "submenu row '" + row.id + "' has no submenu";
        return false;
    }

    bool wrong = false;
    if (Fn f = field_fn(L, t, "get", wrong)) row.get = value_getter(f);
    if (Fn f = field_fn(L, t, "set", wrong)) row.set = value_setter(f);
    if (Fn f = field_fn(L, t, "activate", wrong)) row.activate = action_fn(f);
    if (Fn f = field_fn(L, t, "list", wrong)) row.list = list_fn(f);

    lua_getfield(L, t, "step");  // a number steps a slider, a function steps a setting
    if (lua_type(L, -1) == LUA_TNUMBER) {
        row.step = lua_tonumber(L, -1);
    } else if (lua_type(L, -1) == LUA_TFUNCTION) {
        row.step_fn = step_fn(make_fn(L, -1));
    } else if (lua_type(L, -1) != LUA_TNIL) {
        wrong = true;
    }
    lua_pop(L, 1);

    lua_getfield(L, t, "text");  // a function computes the value text, a string is constant
    if (lua_type(L, -1) == LUA_TFUNCTION) {
        row.text = text_fn(make_fn(L, -1));
    } else if (lua_type(L, -1) == LUA_TSTRING) {
        std::string s = lua_tostring(L, -1);
        row.text = [s]() { return s; };
    } else if (lua_type(L, -1) != LUA_TNIL) {
        wrong = true;
    }
    lua_pop(L, 1);

    for (const char* key : {"visible", "enabled"}) {
        lua_getfield(L, t, key);
        std::function<bool()> fn;
        if (lua_type(L, -1) == LUA_TFUNCTION) {
            fn = bool_fn(make_fn(L, -1), key);
        } else if (lua_type(L, -1) == LUA_TBOOLEAN) {
            const bool b = lua_toboolean(L, -1) != 0;
            fn = [b]() { return b; };
        } else if (lua_type(L, -1) != LUA_TNIL) {
            wrong = true;
        }
        lua_pop(L, 1);
        (key[0] == 'v' ? row.visible : row.enabled) = std::move(fn);
    }
    if (wrong) {
        err = "row '" + row.id + "': a field has the wrong type";
        return false;
    }
    lua_getfield(L, t, "visible_if");
    const bool cond_ok = parse_visible_if(L, -1, row.visible_if);
    lua_pop(L, 1);
    if (!cond_ok) {
        err = "row '" + row.id + "': visible_if needs { key = \"...\", any = {...} / none = {...} / is = v }";
        return false;
    }
    return true;
}

bool parse_rows(lua_State* L, int t, std::vector<RowDef>& rows, std::string& err) {
    if (lua_type(L, t) == LUA_TNIL) {
        return true;
    }
    if (lua_type(L, t) != LUA_TTABLE) {
        err = "rows must be a list";
        return false;
    }
    t = lua_absindex(L, t);
    const lua_Unsigned n = lua_rawlen(L, t);
    for (lua_Unsigned i = 1; i <= n; ++i) {
        lua_rawgeti(L, t, static_cast<lua_Integer>(i));
        RowDef row;
        const bool ok = parse_row(L, -1, row, err);
        lua_pop(L, 1);
        if (!ok) {
            return false;
        }
        rows.push_back(std::move(row));
    }
    return true;
}

// Raises `msg` after the caller's C++ objects are gone (longjmp-safe).
int raise(lua_State* L, const char* msg) { return luaL_error(L, "%s", msg); }

void copy_msg(char (&dst)[256], const std::string& s) { std::snprintf(dst, sizeof dst, "%s", s.c_str()); }

// ---- sh.menu.* ----

int l_define(lua_State* L) {
    char msg[256] = {0};
    if (lua_type(L, 1) != LUA_TTABLE) {
        return raise(L, "sh.menu.define expects a table");
    }
    {
        std::string err;
        MenuDef def;
        field_text(L, 1, "id", def.id);
        field_text(L, 1, "title", def.title);
        if (def.id.empty()) {
            err = "sh.menu.define: id is required";
        } else {
            lua_getfield(L, 1, "rows");
            parse_rows(L, -1, def.rows, err);
            lua_pop(L, 1);
        }
        if (err.empty() && !menu_hooks(L, 1, def)) {
            err = "sh.menu.define: on_open, leave_confirm and on_leave must be functions";
        }
        if (err.empty()) {
            state_of(L)->registry->define(std::move(def));
        } else {
            copy_msg(msg, err);
        }
    }
    return msg[0] ? raise(L, msg) : 0;
}

int l_append_rows(lua_State* L) {
    char msg[256] = {0};
    if (lua_type(L, 1) != LUA_TSTRING || lua_type(L, 2) != LUA_TTABLE) {
        return raise(L, "sh.menu.append_rows(id, rows) expects a string and a list");
    }
    bool found = false;
    {
        std::string err;
        std::vector<RowDef> rows;
        if (parse_rows(L, 2, rows, err)) {
            found = state_of(L)->registry->append_rows(lua_tostring(L, 1), std::move(rows));
        } else {
            copy_msg(msg, err);
        }
    }
    if (msg[0]) {
        return raise(L, msg);
    }
    lua_pushboolean(L, found ? 1 : 0);
    return 1;
}

int l_add_page(lua_State* L) {
    char msg[256] = {0};
    if (lua_type(L, 1) != LUA_TTABLE) {
        return raise(L, "sh.menu.add_page expects a table");
    }
    bool found = false;
    {
        std::string err, parent;
        MenuDef page;
        RowDef link;
        field_text(L, 1, "parent", parent);
        field_text(L, 1, "id", page.id);
        field_text(L, 1, "title", page.title);
        field_text(L, 1, "label", link.label);
        field_text(L, 1, "help", link.help);
        field_text(L, 1, "link_id", link.id);
        if (parent.empty() || page.id.empty()) {
            err = "sh.menu.add_page: parent and id are required";
        } else {
            lua_getfield(L, 1, "rows");
            parse_rows(L, -1, page.rows, err);
            lua_pop(L, 1);
        }
        if (err.empty() && !menu_hooks(L, 1, page)) {
            err = "sh.menu.add_page: on_open, leave_confirm and on_leave must be functions";
        }
        // Options' root is the game's: a page a mod adds there is listed under Mod Options instead. Pages named
        // "options.<...>" are the game's own (Mods, Mod Options).
        const bool moved = parent == kOptionsMenu && !page.id.starts_with("options.");
        if (moved) {
            parent = kModOptionsMenu;
        }
        if (err.empty()) {
            sh::menu::Registry& registry = *state_of(L)->registry;
            found = registry.add_page(parent, std::move(page), std::move(link));
            // Mod Options is defined once every file ran (or its pages fall back to Options): the link is coming.
            found = found || (moved && registry.find(kOptionsMenu) != nullptr);
        } else {
            copy_msg(msg, err);
        }
    }
    if (msg[0]) {
        return raise(L, msg);
    }
    lua_pushboolean(L, found ? 1 : 0);
    return 1;
}

int l_page_count(lua_State* L) {
    const char* id = lua_tostring(L, 1);
    lua_pushinteger(L, id != nullptr ? static_cast<lua_Integer>(state_of(L)->registry->page_count(id)) : 0);
    return 1;
}

// A menu's on_leave or leave_confirm asks the engine's state through the predicates rows use for `visible`.
int l_native_visible(lua_State* L) {
    const char* name = lua_tostring(L, 1);
    const std::optional<bool> shown = name != nullptr ? state_of(L)->registry->check_visible(name) : std::nullopt;
    lua_pushboolean(L, shown.value_or(false) ? 1 : 0);
    return 1;
}

int l_run(lua_State* L) {
    const char* name = lua_tostring(L, 1);
    lua_pushboolean(L, name != nullptr && state_of(L)->registry->run_action(name) ? 1 : 0);
    return 1;
}

int l_value(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING || lua_type(L, 2) != LUA_TFUNCTION ||
        (lua_type(L, 3) != LUA_TFUNCTION && lua_type(L, 3) != LUA_TNIL && lua_type(L, 3) != LUA_TNONE)) {
        return raise(L, "sh.menu.value(name, get, set?) expects a string and functions");
    }
    sh::menu::NativeValue v;
    v.get = value_getter(make_fn(L, 2));
    if (lua_type(L, 3) == LUA_TFUNCTION) {
        v.set = value_setter(make_fn(L, 3));
    }
    state_of(L)->registry->native_value(lua_tostring(L, 1), std::move(v));
    return 0;
}

int l_action(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING || lua_type(L, 2) != LUA_TFUNCTION) {
        return raise(L, "sh.menu.action(name, fn) expects a string and a function");
    }
    state_of(L)->registry->native_action(lua_tostring(L, 1), action_fn(make_fn(L, 2)));
    return 0;
}

int l_list(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING || lua_type(L, 2) != LUA_TFUNCTION) {
        return raise(L, "sh.menu.list(name, fn) expects a string and a function");
    }
    state_of(L)->registry->native_list(lua_tostring(L, 1), list_fn(make_fn(L, 2)));
    return 0;
}

int l_open(lua_State* L) {
    const char* id = lua_tostring(L, 1);
    auto& hooks = sh::menu::host();
    lua_pushboolean(L, id != nullptr && hooks.open && hooks.open(id) ? 1 : 0);
    return 1;
}

int l_close(lua_State*) {
    auto& hooks = sh::menu::host();
    if (hooks.close) {
        hooks.close();
    }
    return 0;
}

int l_is_open(lua_State* L) {
    auto& hooks = sh::menu::host();
    lua_pushboolean(L, hooks.is_open && hooks.is_open() ? 1 : 0);
    return 1;
}

// Lets a file extend a menu another file defines, whatever the file order (menus/mods.lua sorts before
// options.lua, and a mod may define the menu again).
int l_after_load(lua_State* L) {
    if (lua_type(L, 1) != LUA_TFUNCTION) {
        return raise(L, "sh.menu.after_load(fn) expects a function");
    }
    lua_pushvalue(L, 1);
    state_of(L)->after_load.push_back(luaL_ref(L, LUA_REGISTRYINDEX));
    return 0;
}

int l_log(lua_State* L) {
    const char* s = lua_tostring(L, 1);
    state_of(L)->say(0, s ? s : "");
    return 0;
}

void open_sandbox(lua_State* L) {
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(L, 1);
    for (const char* name : {"dofile", "loadfile", "load", "loadstring", "print", "collectgarbage", "require"}) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }

    static const luaL_Reg menu_fns[] = {{"define", l_define}, {"append_rows", l_append_rows}, {"add_page", l_add_page},
                                        {"value", l_value},   {"action", l_action},           {"list", l_list},
                                        {"open", l_open},     {"close", l_close},             {"is_open", l_is_open},
                                        {"after_load", l_after_load}, {"page_count", l_page_count},
                                        {"native_visible", l_native_visible}, {"run", l_run},
                                        {nullptr, nullptr}};
    lua_newtable(L);  // sh
    lua_pushcfunction(L, l_log);
    lua_setfield(L, -2, "log");
    luaL_newlib(L, menu_fns);
    lua_setfield(L, -2, "menu");
    open_mods_api(L);
    open_engine_api(L);
    install_config_table(L, &state_of(L)->config);
    lua_setglobal(L, "sh");
}

std::vector<fs::path> lua_files(const fs::path& dir) {
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return files;
    }
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (e.is_regular_file(ec) && e.path().extension() == ".lua") {
            files.push_back(e.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

void run_file(MenuState& st, const fs::path& file) {
    lua_State* L = st.L;
    if (luaL_loadfilex(L, file.string().c_str(), "t") != LUA_OK) {  // no binary chunks from mods
        st.say(1, file.generic_string() + ": " + (lua_tostring(L, -1) ? lua_tostring(L, -1) : "load error") +
                      " (file skipped)");
        lua_pop(L, 1);
        return;
    }
    if (!protected_call(st, 0, 0, 0, file.generic_string().c_str())) {
        st.say(1, file.generic_string() + ": file skipped (rows it defined before the error stay)");
    }
}

// ---- rows with config = "<key>": bound to the mod settings store when a menu opens ----

bool kind_fits(RowKind kind, SettingType type) {
    switch (kind) {
        case RowKind::Toggle: return type == SettingType::Bool;
        case RowKind::Slider:
        case RowKind::Number: return type == SettingType::Number;
        case RowKind::Choice:
        case RowKind::Dropdown: return type == SettingType::Choice;
        case RowKind::Info: return true;
        default: return false;
    }
}

const char* kind_name(RowKind kind) {
    switch (kind) {
        case RowKind::Toggle: return "toggle";
        case RowKind::Slider: return "slider";
        case RowKind::Number: return "number";
        case RowKind::Choice: return "choice";
        case RowKind::Dropdown: return "dropdown";
        case RowKind::Info: return "info";
        default: return "this kind of";
    }
}

void restore_range(RowDef& row) {
    const RowDef plain;
    if (std::isnan(row.min)) row.min = plain.min;
    if (std::isnan(row.max)) row.max = plain.max;
    if (std::isnan(row.step)) row.step = row.kind == RowKind::Number ? 1.0 : plain.step;
}

bool whole(double v) { return std::isfinite(v) && v == std::floor(v); }

bool bind_config_row(RowDef& row, const MenuLogFn& log, std::set<std::string>& reported) {
    const SettingDecl* d = mod_settings().find(row.config);
    std::string problem;
    if (d == nullptr) {
        problem = "config key '" + row.config + "' is not declared (sh.config.declare)";
    } else if (!kind_fits(row.kind, d->type)) {
        problem = std::string("a ") + kind_name(row.kind) + " row cannot show config key '" + row.config + "'";
    }
    if (!problem.empty()) {
        restore_range(row);
        if (reported.insert(row.config + "|" + row.id).second) {
            const std::string m = "row '" + row.id + "': " + problem;
            if (log) log(1, m);
            else std::fprintf(stderr, "[menu] %s\n", m.c_str());
        }
        return false;
    }
    const std::string key = row.config;
    if (!row.get) {
        row.get = [key]() {
            const auto v = mod_settings().get(key);
            return v ? *v : Value{};
        };
    }
    if (!row.set && row.kind != RowKind::Info) {
        row.set = [key, log](const Value& v) {
            std::string err;
            if (!mod_settings().set(key, v, err) && !err.empty()) {
                if (log) log(1, "sh.config: " + err);
                else std::fprintf(stderr, "[menu] sh.config: %s\n", err.c_str());
            }
        };
    }
    if (row.kind == RowKind::Slider || row.kind == RowKind::Number) {
        const bool own_step = !std::isnan(row.step);
        if (std::isnan(row.min) && std::isfinite(d->min)) row.min = d->min;
        if (std::isnan(row.max) && std::isfinite(d->max)) row.max = d->max;
        if (std::isnan(row.step) && d->step > 0) row.step = d->step;
        restore_range(row);
        // A slider over whole numbers with a whole step shows and stores whole numbers.
        if (row.kind == RowKind::Slider && !own_step && whole(row.min) && whole(row.max) && whole(row.step) &&
            row.step >= 1.0) {
            row.integer = true;
        }
    } else {
        restore_range(row);
    }
    if ((row.kind == RowKind::Choice || row.kind == RowKind::Dropdown) && row.choices.empty() && !row.list) {
        row.choices = d->choices;
    }
    return true;
}

void install_config_binder(sh::menu::Registry& registry, MenuLogFn log) {
    auto reported = std::make_shared<std::set<std::string>>();
    registry.config_binder(
        [log = std::move(log), reported](RowDef& row) { return bind_config_row(row, log, *reported); });
    registry.config_reader("sh.config", [](std::string_view key) { return mod_settings().get(key); });
}

}  // namespace

void load_menu_scripts(const fs::path& gamedata_dir, const fs::path& mods_dir, sh::menu::Registry& registry,
                       MenuLogFn log) {
    install_config_binder(registry, log);
    std::vector<fs::path> files = lua_files(gamedata_dir / "menus");
    std::error_code ec;
    if (!mods_dir.empty() && fs::is_directory(mods_dir, ec)) {
        std::vector<std::string> warnings;
        const auto found = mods::discover(mods_dir, warnings);  // ascending priority: the last define wins
        std::vector<std::string> refused;
        for (const auto& mod : found) {
            if (!mods::scripts_allowed(mod, refused)) {
                continue;
            }
            const auto mod_files = lua_files(mod.root / "menus");
            files.insert(files.end(), mod_files.begin(), mod_files.end());
        }
        for (const std::string& w : refused) {
            if (log) {
                log(1, "mods: " + w);
            }
        }
    }
    if (files.empty()) {
        return;
    }

    auto state = std::make_unique<MenuState>();
    state->registry = &registry;
    state->log = std::move(log);
    state->L = luaL_newstate();
    if (state->L == nullptr) {
        state->say(1, "menus: cannot create the Lua state");
        return;
    }
    *static_cast<MenuState**>(lua_getextraspace(state->L)) = state.get();
    MenuState* raw = state.get();
    raw->config.log = [raw](int level, const std::string& m) { raw->say(level, m); };
    // The menu state lives until the process ends, so its listeners may stay in the store.
    raw->config.listener = [](lua_State* L, int idx) { return value_setter(make_fn(L, idx)); };
    open_sandbox(state->L);
    lua_sethook(state->L, budget_hook, LUA_MASKCOUNT, kHookStep);
    for (const fs::path& f : files) {
        run_file(*state, f);
    }
    for (const int ref : state->after_load) {
        lua_rawgeti(state->L, LUA_REGISTRYINDEX, ref);
        protected_call(*state, 0, 0, 0, "after_load");
        luaL_unref(state->L, LUA_REGISTRYINDEX, ref);
    }
    state->after_load.clear();
    // Menus without a Mod Options page (a mod that replaces the game's menus) keep the mods' pages on Options.
    registry.adopt_orphan_pages(kModOptionsMenu, kOptionsMenu);
    // Reported once every file ran: a parent defined by a later file (options.lua after mods.lua) is normal.
    for (const std::string& parent : registry.unlinked_parents()) {
        state->say(0, "sh.menu.add_page: menu '" + parent + "' is not defined; the pages added to it have no link");
    }
    all_states().push_back(std::move(state));
}

void reload_menu_scripts(const fs::path& gamedata_dir, const fs::path& mods_dir, sh::menu::Registry& registry,
                         MenuLogFn log) {
    registry.clear();
    load_menu_scripts(gamedata_dir, mods_dir, registry, std::move(log));
}

}  // namespace sh::script
