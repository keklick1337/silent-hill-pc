// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/runtime/world_object_binds.h"

#include "engine/runtime/world_object_runtime.h"

#include <cstdio>
#include <iterator>
#include <optional>
#include <string_view>
#include <vector>

namespace sh::world {

namespace {

using NamedCall = int (*)(const char*, int, const ShScriptArg*, long long*, int, char*, int);
using NamedList = const char* const* (*)(void);
NamedCall s_next_call = nullptr;
NamedList s_next_list = nullptr;

constexpr const char* kNames[] = {
    "world_object.position_set", "world_object.rotation_set", "world_object.visible_set",
    "world_object.model_set",    "world_object.position_get", "world_object.rotation_get",
};

// Argument kinds of ShScriptArg: 0 nil, 1 integer, 2 string, 3 boolean.
struct Args {
    int argc;
    const ShScriptArg* argv;
    bool is_integer(int i) const { return i < argc && (argv[i].kind == 1 || argv[i].kind == 3); }
    long long integer(int i) const { return is_integer(i) ? argv[i].i : 0; }
    const char* text(int i) const { return i < argc && argv[i].kind == 2 ? argv[i].s : nullptr; }
};

int fail(char* err, int cap, const char* msg) {
    std::snprintf(err, static_cast<std::size_t>(cap), "%s", msg);
    return -1;
}

int done(const Result<void>& r, char* err, int cap) { return r ? 0 : fail(err, cap, r.error().c_str()); }

int call(std::string_view fn, const Args& a, long long* out, char* err, int cap) {
    const char* name = a.text(0);
    if (name == nullptr) {
        return fail(err, cap, "world_object.* takes the object name as a string");
    }
    WorldObjectRuntime& objects = world_object_runtime();
    if (fn == "position_set" || fn == "rotation_set") {
        if (!a.is_integer(1) || !a.is_integer(2) || !a.is_integer(3)) {
            return fail(err, cap, "world_object.*_set takes x, y, z as integers");
        }
        const auto x = static_cast<std::int32_t>(a.integer(1));
        const auto y = static_cast<std::int32_t>(a.integer(2));
        const auto z = static_cast<std::int32_t>(a.integer(3));
        return done(fn == "position_set" ? objects.set_position(name, x, y, z) : objects.set_rotation(name, x, y, z),
                    err, cap);
    }
    if (fn == "visible_set") {
        // true / false, or nil: by the data again
        std::optional<bool> visible;
        if (a.argc > 1 && a.argv[1].kind == 3) {
            visible = a.argv[1].i != 0;
        } else if (a.argc > 1 && a.argv[1].kind != 0) {
            return fail(err, cap, "world_object.visible_set takes true, false or nil");
        }
        return done(objects.set_visible(name, visible), err, cap);
    }
    if (fn == "model_set") {
        const char* model = a.text(1);
        if (model == nullptr) {
            return fail(err, cap, "world_object.model_set takes the model name as a string");
        }
        return done(objects.set_model(name, model), err, cap);
    }
    if (fn == "position_get" || fn == "rotation_get") {
        const Result<WorldObjectRuntime::Pose> pose = objects.pose(name);
        if (!pose) {
            return fail(err, cap, pose.error().c_str());
        }
        for (std::size_t i = 0; i < 3; ++i) {
            out[i] = fn == "position_get" ? pose.value().position[i] : pose.value().rotation[i];
        }
        return 3;
    }
    return fail(err, cap, "unknown world_object call");
}

int named_call(const char* name, int argc, const ShScriptArg* argv, long long* out, int out_cap, char* err,
               int err_cap) {
    const std::string_view full(name != nullptr ? name : "");
    if (full.rfind("world_object.", 0) == 0) {
        if (out_cap < 3) {
            return fail(err, err_cap, "output too small");
        }
        return call(full.substr(13), Args{argc, argv}, out, err, err_cap);
    }
    return s_next_call != nullptr ? s_next_call(name, argc, argv, out, out_cap, err, err_cap)
                                  : fail(err, err_cap, "not available");
}

const char* const* named_list() {
    static std::vector<const char*> names;
    if (names.empty()) {
        if (s_next_list != nullptr) {
            for (const char* const* n = s_next_list(); n != nullptr && *n != nullptr; ++n) {
                names.push_back(*n);
            }
        }
        names.insert(names.end(), std::begin(kNames), std::end(kNames));
        names.push_back(nullptr);
    }
    return names.data();
}

}  // namespace

void bind_world_object_api(ShScriptGameApi* api) {
    s_next_call = api->named_call;
    s_next_list = api->named_list;
    api->named_call = named_call;
    api->named_list = named_list;
}

}  // namespace sh::world
