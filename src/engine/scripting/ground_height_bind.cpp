// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Lua: world.ground_height(x, z) -> the ground height (Q12) under a world point, from the engine's ground cache.
// Chained in front of the named calls bound before it (registration order: sh_script.h).

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/script/sh_script.h"
#include "engine/world/ground_point.h"

namespace {

using NamedCall = int (*)(const char*, int, const ShScriptArg*, long long*, int, char*, int);
using NamedList = const char* const* (*)(void);

NamedCall g_next_call = nullptr;
NamedList g_next_list = nullptr;

constexpr const char* kName = "world.ground_height";

int named_call(const char* name, int argc, const ShScriptArg* argv, long long* out, int out_cap, char* err,
               int err_cap) {
    if (std::strcmp(name, kName) == 0) {
        const auto integer = [](const ShScriptArg& a) { return a.kind == 1; };
        if (argc < 2 || !integer(argv[0]) || !integer(argv[1]) || out_cap < 1) {
            std::snprintf(err, static_cast<size_t>(err_cap), "%s takes a world position (x, z)", name);
            return -1;
        }
        out[0] = sh::world::ground_point_cache().height(static_cast<std::int32_t>(argv[0].i),
                                                        static_cast<std::int32_t>(argv[1].i));
        return 1;
    }
    if (g_next_call == nullptr) {
        std::snprintf(err, static_cast<size_t>(err_cap), "unknown function '%s'", name);
        return -1;
    }
    return g_next_call(name, argc, argv, out, out_cap, err, err_cap);
}

const char* const* named_list() {
    static std::vector<const char*> ptrs;
    static std::vector<std::string> names;
    if (!ptrs.empty()) {
        return ptrs.data();
    }
    if (g_next_list != nullptr) {
        for (const char* const* n = g_next_list(); n != nullptr && *n != nullptr; ++n) {
            names.emplace_back(*n);
        }
    }
    names.emplace_back(kName);
    for (const std::string& s : names) {
        ptrs.push_back(s.c_str());
    }
    ptrs.push_back(nullptr);
    return ptrs.data();
}

void bind_ground_height(ShScriptGameApi* api) {
    g_next_call = api->named_call;
    g_next_list = api->named_list;
    api->named_call = named_call;
    api->named_list = named_list;
}

const ShScriptGameBinderEntry s_ScriptBinder(310, bind_ground_height);

}  // namespace
