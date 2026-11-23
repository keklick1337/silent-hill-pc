// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// sh.hooks.* (the engine event bus, engine/hooks/hook_bus.h, from Lua), sh.mods.get and mod.export
// ("Hooks" and "Mods talking to each other").
//
//   sh.hooks.on(event, fn[, {priority = 0, id = "name"}]) -> handle
//   sh.hooks.off(handle) / sh.hooks.off(event, id)          -> true when a handler went away
//   sh.hooks.emit(event[, fields[, cancellable]])           -> fields, cancelled
//   sh.hooks.list([event])                                  -> {{event, owner, id, priority, handle}, ...}
//   sh.hooks.has(event)                                     -> true when anyone listens
//   fn(ev): ev.<field> reads, ev.<field> = v writes (refused for read-only fields), ev:cancel(), ev.cancelled,
//           ev.name, ev.cancellable, ev:get(key), ev:set(key, v) -> ok, ev:fields() -> a copy as a table
//   sh.mods.get(id) -> {id, name, version, api} of an active mod (api: its mod.export() table in this state)
//                      or nil. A mod reaching another's api at load time must name it in "dependencies" or
//                      "optional_dependencies": only that makes the other one load (and export) first.

#include "engine/hooks/hook_bus.h"
#include "engine/mods/mods.h"
#include "engine/script/script_host.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace sh::script {

namespace {

char kExportsKey = 0;

// A handler's view of the event; `event` is cleared when the handler returns, so a kept proxy cannot reach a
// finished event.
struct EventBox {
    hooks::Event* event = nullptr;
    Host* host = nullptr;
    const std::string* owner = nullptr;
};

constexpr const char* kEventMethods[] = {"name", "cancellable", "cancelled", "cancel", "get", "set", "fields"};

bool reserved(std::string_view key) {
    for (const char* m : kEventMethods) {
        if (key == m) {
            return true;
        }
    }
    return false;
}

Host* host_up(lua_State* L) {
    return static_cast<Host*>(lua_touserdata(L, lua_upvalueindex(1)));
}

// The owner bound to this sh.hooks table: a mod id, or (empty) the state's own globals.
std::string owner_up(lua_State* L) {
    const char* o = lua_tostring(L, lua_upvalueindex(2));
    if (o != nullptr && o[0] != '\0') {
        return o;
    }
    return host_up(L)->base_owner();
}

void push_value(lua_State* L, const hooks::Value& v) {
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

// Lua is built as C: its errors longjmp over C++ frames, so every check that can raise runs before a C++ object
// with a destructor exists in the calling function.
void check_value(lua_State* L, int idx) {
    const int t = lua_type(L, idx);
    if (t != LUA_TNIL && t != LUA_TNONE && t != LUA_TBOOLEAN && t != LUA_TNUMBER && t != LUA_TSTRING) {
        luaL_error(L, "event fields hold only nil, booleans, numbers and strings (got a %s)", luaL_typename(L, idx));
    }
}

// `like` keeps an integer field an integer when a handler writes 5.0 into it. The value was check_value()d.
hooks::Value to_value(lua_State* L, int idx, const hooks::Value* like) {
    switch (lua_type(L, idx)) {
    case LUA_TNIL:
    case LUA_TNONE: return std::monostate{};
    case LUA_TBOOLEAN: return lua_toboolean(L, idx) != 0;
    case LUA_TNUMBER: {
        if (lua_isinteger(L, idx)) {
            return static_cast<std::int64_t>(lua_tointeger(L, idx));
        }
        const double d = lua_tonumber(L, idx);
        if (like != nullptr && std::holds_alternative<std::int64_t>(*like) && std::isfinite(d) &&
            d == std::floor(d) && std::fabs(d) < 9.0e18) {
            return static_cast<std::int64_t>(d);
        }
        return d;
    }
    case LUA_TSTRING: {
        std::size_t n = 0;
        const char* s = lua_tolstring(L, idx, &n);
        return std::string(s, n);
    }
    default: return std::monostate{};
    }
}

EventBox* check_box(lua_State* L, int idx) {
    auto* box = static_cast<EventBox*>(luaL_checkudata(L, idx, "sh.hooks.event"));
    if (box->event == nullptr) {
        luaL_error(L, "this event is over (an event can be used only while its handler runs)");
    }
    return box;
}

// A write of the check_value()d value; false (with a warning once per owner, event and field) for a read-only
// field.
bool write_field(lua_State* L, EventBox* box, std::string_view key, int value_idx) {
    hooks::Event& ev = *box->event;
    if (ev.update(key, to_value(L, value_idx, ev.get(key)))) {
        return true;
    }
    box->host->warn_once("ro|" + *box->owner + "|" + ev.name() + "|" + std::string(key),
                         "hook " + ev.name() + " of " + *box->owner + ": field '" + std::string(key) +
                             "' is read-only; the write is ignored");
    return false;
}

bool cancel_event(EventBox* box) {
    hooks::Event& ev = *box->event;
    if (ev.cancel(*box->owner)) {
        return true;
    }
    box->host->warn_once("nc|" + *box->owner + "|" + ev.name(),
                         "hook " + ev.name() + " of " + *box->owner + ": the event cannot be cancelled");
    return false;
}

int l_event_cancel(lua_State* L) {
    EventBox* box = check_box(L, 1);
    lua_pushboolean(L, cancel_event(box) ? 1 : 0);
    return 1;
}

int l_event_get(lua_State* L) {
    EventBox* box = check_box(L, 1);
    const hooks::Value* v = box->event->get(luaL_checkstring(L, 2));
    if (v == nullptr) {
        lua_pushnil(L);
    } else {
        push_value(L, *v);
    }
    return 1;
}

int l_event_set(lua_State* L) {
    EventBox* box = check_box(L, 1);
    const char* key = luaL_checkstring(L, 2);
    check_value(L, 3);
    const bool ok = write_field(L, box, key, 3);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

void push_fields(lua_State* L, const hooks::Event& ev) {
    lua_createtable(L, 0, static_cast<int>(ev.fields().size()));
    for (const hooks::Event::Field& f : ev.fields()) {
        push_value(L, f.value);
        lua_setfield(L, -2, f.key.c_str());
    }
}

int l_event_fields(lua_State* L) {
    push_fields(L, *check_box(L, 1)->event);
    return 1;
}

int l_event_index(lua_State* L) {
    EventBox* box = check_box(L, 1);
    const hooks::Event& ev = *box->event;
    std::size_t n = 0;
    const char* k = luaL_checklstring(L, 2, &n);
    const std::string_view key(k, n);
    if (key == "name") {
        lua_pushstring(L, ev.name().c_str());
    } else if (key == "cancellable") {
        lua_pushboolean(L, ev.cancellable() ? 1 : 0);
    } else if (key == "cancelled") {
        lua_pushboolean(L, ev.cancelled() ? 1 : 0);
    } else if (key == "cancel") {
        lua_pushcfunction(L, l_event_cancel);
    } else if (key == "get") {
        lua_pushcfunction(L, l_event_get);
    } else if (key == "set") {
        lua_pushcfunction(L, l_event_set);
    } else if (key == "fields") {
        lua_pushcfunction(L, l_event_fields);
    } else if (const hooks::Value* v = ev.get(key)) {
        push_value(L, *v);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

int l_event_newindex(lua_State* L) {
    EventBox* box = check_box(L, 1);
    const char* key = luaL_checkstring(L, 2);
    if (std::string_view(key) == "cancelled") {
        if (lua_toboolean(L, 3) != 0) {
            cancel_event(box);
        }
        return 0;  // an event cannot be un-cancelled: a later handler never runs once one cancelled
    }
    if (reserved(key)) {
        return luaL_error(L, "'%s' is a property of the event; use ev:set('%s', value)", key, key);
    }
    check_value(L, 3);
    write_field(L, box, key, 3);
    return 0;
}

void push_event(lua_State* L, EventBox** out) {
    auto* box = static_cast<EventBox*>(lua_newuserdatauv(L, sizeof(EventBox), 0));
    *box = EventBox{};
    if (luaL_newmetatable(L, "sh.hooks.event") != 0) {
        lua_pushcfunction(L, l_event_index);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, l_event_newindex);
        lua_setfield(L, -2, "__newindex");
        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "__metatable");
    }
    lua_setmetatable(L, -2);
    *out = box;
}

int l_hooks_on(lua_State* L) {
    Host* host = host_up(L);
    const char* event = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    if (event[0] == '\0') {
        return luaL_error(L, "sh.hooks.on: the event name is empty");
    }
    int priority = 0;
    const char* id = "";
    if (!lua_isnoneornil(L, 3)) {
        luaL_checktype(L, 3, LUA_TTABLE);
        if (lua_getfield(L, 3, "priority") != LUA_TNIL) {
            if (lua_type(L, -1) != LUA_TNUMBER) {
                return luaL_error(L, "sh.hooks.on: priority must be a number");
            }
            const double p = lua_tonumber(L, -1);
            priority = p > 1e6 ? 1000000 : p < -1e6 ? -1000000 : static_cast<int>(p);
        }
        lua_pop(L, 1);
        if (lua_getfield(L, 3, "id") != LUA_TNIL) {
            if (lua_type(L, -1) != LUA_TSTRING) {
                return luaL_error(L, "sh.hooks.on: id must be a string");
            }
            id = lua_tostring(L, -1);  // the options table keeps the string alive
        }
        lua_pop(L, 1);
    }
    lua_pushvalue(L, 2);
    const hooks::Token token = host->hook_on(L, owner_up(L), event, priority, id);
    lua_pushinteger(L, static_cast<lua_Integer>(token));
    return 1;
}

int l_hooks_off(lua_State* L) {
    Host* host = host_up(L);
    if (lua_type(L, 1) == LUA_TNUMBER) {
        lua_pushboolean(L, host->hook_off(static_cast<hooks::Token>(lua_tointeger(L, 1))) ? 1 : 0);
        return 1;
    }
    const char* event = luaL_checkstring(L, 1);
    const char* id = luaL_checkstring(L, 2);
    lua_pushboolean(L, host->hook_off(owner_up(L), event, id) ? 1 : 0);
    return 1;
}

// Nested emits (a handler emitting the event it handles) end here instead of in a C stack overflow.
int g_emit_depth = 0;
constexpr int kMaxEmitDepth = 16;

int l_hooks_emit(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const bool fields = !lua_isnoneornil(L, 2);
    if (fields) {
        luaL_checktype(L, 2, LUA_TTABLE);
        lua_pushnil(L);
        while (lua_next(L, 2) != 0) {
            if (lua_type(L, -2) == LUA_TSTRING) {  // lua_next must not see a key converted in place
                check_value(L, -1);
            }
            lua_pop(L, 1);
        }
    }
    if (g_emit_depth >= kMaxEmitDepth) {
        return luaL_error(L, "sh.hooks.emit('%s'): events nested more than %d deep", name, kMaxEmitDepth);
    }
    const bool cancellable = lua_toboolean(L, 3) != 0;
    bool done = true;
    {
        hooks::Event ev(name, cancellable);
        if (fields) {
            lua_pushnil(L);
            while (lua_next(L, 2) != 0) {
                if (lua_type(L, -2) == LUA_TSTRING) {
                    ev.set(lua_tostring(L, -2), to_value(L, -1, nullptr));
                }
                lua_pop(L, 1);
            }
        }
        ++g_emit_depth;
        done = hooks::bus().emit(ev);
        --g_emit_depth;
        push_fields(L, ev);
    }
    lua_pushboolean(L, done ? 0 : 1);
    return 2;
}

int l_hooks_list(lua_State* L) {
    const char* event = luaL_optstring(L, 1, "");
    const std::vector<hooks::HandlerInfo> list = hooks::bus().handlers(event);
    lua_createtable(L, static_cast<int>(list.size()), 0);
    lua_Integer n = 0;
    for (const hooks::HandlerInfo& h : list) {
        lua_createtable(L, 0, 5);
        lua_pushstring(L, h.event.c_str());
        lua_setfield(L, -2, "event");
        lua_pushstring(L, h.owner.c_str());
        lua_setfield(L, -2, "owner");
        lua_pushstring(L, h.id.c_str());
        lua_setfield(L, -2, "id");
        lua_pushinteger(L, h.priority);
        lua_setfield(L, -2, "priority");
        lua_pushinteger(L, static_cast<lua_Integer>(h.token));
        lua_setfield(L, -2, "handle");
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

int l_hooks_has(lua_State* L) {
    lua_pushboolean(L, hooks::bus().has(luaL_checkstring(L, 1)) ? 1 : 0);
    return 1;
}

int l_mods_get(lua_State* L) {
    Host* host = host_up(L);
    const std::string id = luaL_checkstring(L, 1);
    std::vector<std::string> warnings;
    const std::vector<mods::ModInfo> active = mods::discover(host->config().mods_dir, warnings);
    for (const mods::ModInfo& m : active) {
        if (m.id != id) {
            continue;
        }
        lua_createtable(L, 0, 4);
        lua_pushstring(L, m.id.c_str());
        lua_setfield(L, -2, "id");
        lua_pushstring(L, m.name.c_str());
        lua_setfield(L, -2, "name");
        lua_pushstring(L, m.version.c_str());
        lua_setfield(L, -2, "version");
        push_exports(L);
        lua_getfield(L, -1, id.c_str());
        lua_remove(L, -2);
        lua_setfield(L, -2, "api");
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

}  // namespace

void push_exports(lua_State* L) {
    if (lua_rawgetp(L, LUA_REGISTRYINDEX, &kExportsKey) != LUA_TTABLE) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_rawsetp(L, LUA_REGISTRYINDEX, &kExportsKey);
    }
}

void push_hooks_api(lua_State* L, Host* host, const std::string& owner) {
    lua_createtable(L, 0, 5);
    for (const auto& [name, fn] : {std::pair<const char*, lua_CFunction>{"on", l_hooks_on},
                                   {"off", l_hooks_off}, {"emit", l_hooks_emit}, {"list", l_hooks_list},
                                   {"has", l_hooks_has}}) {
        lua_pushlightuserdata(L, host);
        lua_pushstring(L, owner.c_str());
        lua_pushcclosure(L, fn, 2);
        lua_setfield(L, -2, name);
    }
}

void push_mods_get(lua_State* L, Host* host) {
    lua_pushlightuserdata(L, host);
    lua_pushcclosure(L, l_mods_get, 1);
}

hooks::Token Host::hook_on(lua_State* L, const std::string& owner, std::string_view event, int priority,
                           const std::string& id) {
    auto hook = std::make_shared<LuaHook>();
    hook->event = std::string(event);
    hook->owner = owner;
    hook->id = id;
    hook->ref = luaL_ref(L, LUA_REGISTRYINDEX);  // the registry is the state's, whichever thread calls
    std::weak_ptr<bool> alive = alive_;
    hooks::HandlerOptions options;
    options.owner = owner;
    options.id = id;
    options.priority = priority;
    const hooks::Token token = hooks::bus().on(
        event,
        [this, alive, hook](hooks::Event& ev) {
            const std::shared_ptr<bool> live = alive.lock();
            if (live && *live) {
                dispatch_hook(hook, ev);
            }
        },
        std::move(options), hook_group_);
    hook->token = token;
    if (const auto old = lua_hooks_.find(token); old != lua_hooks_.end()) {
        luaL_unref(L_, LUA_REGISTRYINDEX, old->second->ref);  // the same id again: the new function replaces it
        old->second = hook;
    } else {
        lua_hooks_.emplace(token, hook);
    }
    return token;
}

bool Host::hook_off(hooks::Token token) {
    const auto it = lua_hooks_.find(token);
    if (it == lua_hooks_.end()) {
        return false;  // not this state's: a mod cannot drop another state's handlers by number
    }
    hooks::bus().off(token);
    if (L_ != nullptr) {
        luaL_unref(L_, LUA_REGISTRYINDEX, it->second->ref);
    }
    it->second->ref = kNoRef;
    lua_hooks_.erase(it);
    return true;
}

bool Host::hook_off(const std::string& owner, std::string_view event, std::string_view id) {
    for (const auto& [token, hook] : lua_hooks_) {
        if (hook->owner == owner && hook->event == event && hook->id == id) {
            return hook_off(token);
        }
    }
    return false;
}

void Host::dispatch_hook(const std::shared_ptr<LuaHook>& hook, hooks::Event& event) {
    if (L_ == nullptr || hook->ref == kNoRef) {
        return;
    }
    EventBox* box = nullptr;
    push_event(L_, &box);
    box->event = &event;
    box->host = this;
    box->owner = &hook->owner;
    lua_rawgeti(L_, LUA_REGISTRYINDEX, hook->ref);
    lua_pushvalue(L_, -2);
    const int rc = pcall_budget(1, 0);
    std::string error;
    if (rc != LUA_OK) {
        error = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "unknown error";
        lua_pop(L_, 1);
    }
    box->event = nullptr;
    lua_pop(L_, 1);  // the proxy
    if (rc != LUA_OK) {
        // A broken handler is dropped, not retried every frame; the event and the other handlers go on.
        warn_once("err|" + hook->owner + "|" + event.name(),
                  "hook " + event.name() + " of " + hook->owner + " error: " + error + " (the handler is off)");
        hook_off(hook->token);
    }
}

}  // namespace sh::script
