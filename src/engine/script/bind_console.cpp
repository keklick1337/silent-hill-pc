// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/script/bind_console.h"

#include "engine/console/instance.h"
#include "engine/console/registry.h"
#include "engine/script/script_host.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace sh::script {

namespace {

namespace con = sh::console;

// Registry refs of one registered command; released when it is replaced or removed.
struct CommandRefs {
    std::vector<int> refs;
};

struct HostState {
    std::string owner;
    std::map<std::string, CommandRefs> commands;  // canonical name
};

std::map<Host*, HostState>& states() {
    static auto* s = new std::map<Host*, HostState>;  // leaked: a Host held by another static unregisters at exit
    return *s;
}

Host* host_of(lua_State* L) { return static_cast<Host*>(lua_touserdata(L, lua_upvalueindex(1))); }

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Output used when no console is active: the host log.
class HostLogOutput final : public con::Output {
public:
    explicit HostLogOutput(Host* host) : host_(host) {}
    void print(con::Level level, std::string_view line) override {
        host_->log(level == con::Level::Warn || level == con::Level::Error ? 1 : 0, std::string(line));
    }

private:
    Host* host_;
};

std::string field_string(lua_State* L, int idx, const char* key) {
    std::string out;
    if (lua_getfield(L, idx, key) == LUA_TSTRING) out = lua_tostring(L, -1);
    lua_pop(L, 1);
    return out;
}

std::vector<std::string> field_strings(lua_State* L, int idx, const char* key) {
    std::vector<std::string> out;
    if (lua_getfield(L, idx, key) == LUA_TTABLE) {
        const lua_Integer n = luaL_len(L, -1);
        for (lua_Integer i = 1; i <= n; ++i) {
            if (lua_geti(L, -1, i) == LUA_TSTRING) out.emplace_back(lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    return out;
}

bool field_bool(lua_State* L, int idx, const char* key) {
    lua_getfield(L, idx, key);
    const bool v = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return v;
}

// Takes the function at field `key` into a registry ref (LUA_NOREF when absent).
int field_function_ref(lua_State* L, int idx, const char* key) {
    if (lua_getfield(L, idx, key) == LUA_TFUNCTION) return luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pop(L, 1);
    return LUA_NOREF;
}

bool parse_type(const std::string& t, con::ArgType& out) {
    const std::string s = lower(t);
    if (s == "string") out = con::ArgType::String;
    else if (s == "int") out = con::ArgType::Int;
    else if (s == "float" || s == "number") out = con::ArgType::Float;
    else if (s == "bool") out = con::ArgType::Bool;
    else if (s == "enum") out = con::ArgType::Enum;
    else if (s == "rest") out = con::ArgType::Rest;
    else return false;
    return true;
}

// Pushes the table the run function receives: positional values converted by the spec's type, named access,
// and n = the word count (set last so an argument called "n" cannot hide it).
void push_args(lua_State* L, const std::vector<con::ArgType>& types, const std::vector<std::string>& names,
               const con::Args& args) {
    lua_createtable(L, static_cast<int>(args.size()), static_cast<int>(names.size()) + 1);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const con::ArgType type = i < types.size() ? types[i] : con::ArgType::String;
        switch (type) {
            case con::ArgType::Int: lua_pushinteger(L, static_cast<lua_Integer>(args.integer(i))); break;
            case con::ArgType::Float: lua_pushnumber(L, static_cast<lua_Number>(args.number(i))); break;
            case con::ArgType::Bool: lua_pushboolean(L, args.boolean(i) ? 1 : 0); break;
            default: lua_pushlstring(L, args.str(i).data(), args.str(i).size()); break;
        }
        lua_pushvalue(L, -1);
        lua_rawseti(L, -3, static_cast<lua_Integer>(i + 1));
        if (i < names.size() && !names[i].empty()) {
            lua_setfield(L, -2, names[i].c_str());
        } else {
            lua_pop(L, 1);
        }
    }
    lua_pushinteger(L, static_cast<lua_Integer>(args.size()));
    lua_setfield(L, -2, "n");
}

con::CompleteFn make_complete(Host* host, int ref) {
    return [host, ref](std::string_view prefix, std::vector<con::Candidate>& out) {
        const std::string p(prefix);
        host->call_ref(
            ref,
            [&p](lua_State* S) {
                lua_pushlstring(S, p.data(), p.size());
                return 1;
            },
            [&out](lua_State* S) {
                if (!lua_istable(S, -1)) return;
                const lua_Integer count = luaL_len(S, -1);
                for (lua_Integer k = 1; k <= count; ++k) {
                    const int t = lua_geti(S, -1, k);
                    if (t == LUA_TSTRING) {
                        out.push_back({lua_tostring(S, -1), {}});
                    } else if (t == LUA_TTABLE) {
                        con::Candidate c;
                        if (lua_getfield(S, -1, "text") == LUA_TSTRING) c.text = lua_tostring(S, -1);
                        lua_pop(S, 1);
                        if (lua_getfield(S, -1, "note") == LUA_TSTRING) c.note = lua_tostring(S, -1);
                        lua_pop(S, 1);
                        if (!c.text.empty()) out.push_back(std::move(c));
                    }
                    lua_pop(S, 1);
                }
            },
            nullptr);  // completion errors are ignored (logged by the host)
    };
}

con::RunFn make_run(Host* host, int ref, std::vector<con::ArgType> types, std::vector<std::string> names) {
    return [host, ref, types = std::move(types), names = std::move(names)](con::Context& ctx,
                                                                         const con::Args& args) -> Result<void> {
        std::string error;
        std::vector<std::string> lines;
        const bool ok = host->call_ref(
            ref,
            [&](lua_State* S) {
                push_args(S, types, names, args);
                return 1;
            },
            [&lines](lua_State* S) {
                if (lua_type(S, -1) == LUA_TSTRING) {
                    lines.emplace_back(lua_tostring(S, -1));
                } else if (lua_istable(S, -1)) {
                    const lua_Integer count = luaL_len(S, -1);
                    for (lua_Integer k = 1; k <= count; ++k) {
                        if (lua_geti(S, -1, k) == LUA_TSTRING) lines.emplace_back(lua_tostring(S, -1));
                        lua_pop(S, 1);
                    }
                }
            },
            &error);
        if (!ok) return Result<void>::fail(error);
        for (const std::string& line : lines) ctx.out.info(line);
        return Result<void>::ok();
    };
}

int l_register(lua_State* L) {
    Host* host = host_of(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    HostState& hs = states()[host];

    con::CommandSpec spec;
    spec.name = lower(field_string(L, 1, "name"));
    if (spec.name.empty()) return luaL_error(L, "sh.console.register: 'name' is required");
    const std::string name = spec.name;
    spec.help = field_string(L, 1, "help");
    spec.details = field_string(L, 1, "details");
    spec.group = field_string(L, 1, "group");
    spec.aliases = field_strings(L, 1, "aliases");
    spec.owner = hs.owner;

    CommandRefs refs;
    const int run_ref = field_function_ref(L, 1, "run");
    if (run_ref == LUA_NOREF) return luaL_error(L, "sh.console.register '%s': 'run' must be a function", name.c_str());
    refs.refs.push_back(run_ref);

    std::vector<con::ArgType> types;
    std::vector<std::string> names;
    std::string failure;
    if (lua_getfield(L, 1, "args") == LUA_TTABLE) {
        const lua_Integer n = luaL_len(L, -1);
        for (lua_Integer i = 1; i <= n && failure.empty(); ++i) {
            if (lua_geti(L, -1, i) != LUA_TTABLE) {
                failure = "argument " + std::to_string(i) + " must be a table";
                lua_pop(L, 1);
                break;
            }
            con::ArgSpec a;
            a.name = field_string(L, -1, "name");
            a.help = field_string(L, -1, "help");
            a.optional = field_bool(L, -1, "optional");
            a.values = field_strings(L, -1, "values");
            const std::string type = field_string(L, -1, "type");
            if (!type.empty() && !parse_type(type, a.type)) {
                failure = "argument '" + a.name + "': unknown type '" + type + "'";
            } else if (a.type == con::ArgType::Enum && a.values.empty()) {
                failure = "argument '" + a.name + "': enum needs 'values'";
            }
            const int complete_ref = field_function_ref(L, -1, "complete");
            if (complete_ref != LUA_NOREF) {
                refs.refs.push_back(complete_ref);
                a.complete = make_complete(host, complete_ref);
            }
            types.push_back(a.type);
            names.push_back(a.name);
            spec.args.push_back(std::move(a));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    if (failure.empty()) {
        spec.run = make_run(host, run_ref, std::move(types), std::move(names));
        const Result<void> added = con::global_registry().add(std::move(spec));
        if (!added) {
            failure = added.error();
        } else {
            CommandRefs& slot = hs.commands[name];
            for (const int r : slot.refs) luaL_unref(L, LUA_REGISTRYINDEX, r);
            slot = std::move(refs);
            lua_pushboolean(L, 1);
            return 1;
        }
    }
    for (const int r : refs.refs) luaL_unref(L, LUA_REGISTRYINDEX, r);
    return luaL_error(L, "sh.console.register: %s", failure.c_str());
}

int l_unregister(lua_State* L) {
    Host* host = host_of(L);
    const std::string name = lower(luaL_checkstring(L, 1));
    HostState& hs = states()[host];
    auto it = hs.commands.find(name);
    if (it == hs.commands.end()) {
        lua_pushboolean(L, 0);  // not ours: scripts never remove engine or other mods' commands
        return 1;
    }
    con::global_registry().remove(name);
    for (const int r : it->second.refs) luaL_unref(L, LUA_REGISTRYINDEX, r);
    hs.commands.erase(it);
    lua_pushboolean(L, 1);
    return 1;
}

int l_print(lua_State* L) {
    Host* host = host_of(L);
    const std::string text = luaL_checkstring(L, 1);
    const std::string level = lua_isstring(L, 2) ? lower(lua_tostring(L, 2)) : "info";
    con::Level lv = con::Level::Info;
    if (level == "warn") lv = con::Level::Warn;
    else if (level == "error") lv = con::Level::Error;
    else if (level == "plain") lv = con::Level::Plain;
    if (con::Output* out = con::active_output()) {
        out->print(lv, text);
    } else {
        HostLogOutput(host).print(lv, text);
    }
    return 0;
}

int l_exec(lua_State* L) {
    Host* host = host_of(L);
    const std::string line = luaL_checkstring(L, 1);
    HostLogOutput fallback(host);
    con::Output* out = con::active_output();
    lua_pushboolean(L, con::global_registry().execute(line, out ? *out : fallback) ? 1 : 0);
    return 1;
}

}  // namespace

void install_console_family(lua_State* L, Host* host) {
    HostState& hs = states()[host];
    char buf[40];
    std::snprintf(buf, sizeof buf, "lua:%llx", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(host)));
    hs.owner = buf;

    static const luaL_Reg kFuncs[] = {
        {"register", l_register}, {"unregister", l_unregister}, {"print", l_print}, {"exec", l_exec}, {nullptr, nullptr}};
    lua_createtable(L, 0, 4);
    for (const luaL_Reg* f = kFuncs; f->name != nullptr; ++f) {
        lua_pushlightuserdata(L, host);
        lua_pushcclosure(L, f->func, 1);
        lua_setfield(L, -2, f->name);
    }
    lua_setfield(L, -2, "console");
}

void remove_console_family(Host* host) {
    auto it = states().find(host);
    if (it == states().end()) return;
    con::global_registry().remove_owner(it->second.owner);
    states().erase(it);  // the refs die with the Lua state
}

}  // namespace sh::script
