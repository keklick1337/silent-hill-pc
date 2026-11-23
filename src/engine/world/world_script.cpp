// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/world/world_script.h"

#include <cstdio>
#include <cstring>
#include <iterator>
#include <vector>

namespace sh::world {

namespace {

constexpr const char* kNames[] = {
    "world.ground", "world.ray", "world.cell", "world.collision_groups", "world.speed_zone", "world.ray_surface",
};

// The world cell grid: 40 m squares, counted in Q8 like the game's cell lookup.
constexpr std::int32_t kCellSizeQ8 = (40 * 4096) >> 4;

// Every answer of the group fits in this many integers; a smaller buffer is refused for every name alike.
constexpr int kAnswerMax = 8;

struct Args {
    int argc;
    const ShScriptArg* argv;

    // Integers and booleans (kinds 1 and 3) read as integers; anything else is the fallback.
    long long integer(int i, long long fallback = 0) const {
        return i < argc && (argv[i].kind == 1 || argv[i].kind == 3) ? argv[i].i : fallback;
    }
    std::int32_t q12(int i) const { return static_cast<std::int32_t>(integer(i)); }
    const char* text(int i) const { return i < argc && argv[i].kind == 2 ? argv[i].s : nullptr; }
};

int fail(char* err, int cap, const char* msg) {
    std::snprintf(err, static_cast<std::size_t>(cap), "%s", msg);
    return -1;
}

// The game's floor division of a coordinate to its grid step.
std::int32_t floor_to_step(std::int32_t v, std::int32_t step) {
    return v >= 0 ? v / step : (v - (step - 1)) / step;
}

Vec3 point(const Args& a, int first) { return Vec3{a.q12(first), a.q12(first + 1), a.q12(first + 2)}; }

bool is_world_name(std::string_view fn) {
    for (const char* n : kNames) {
        if (fn == std::string_view(n).substr(6)) {
            return true;
        }
    }
    return false;
}

int call(WorldScriptHost& host, std::string_view fn, const Args& a, long long* out, char* err, int err_cap) {
    if (fn == "ground") {
        // ground(x, z) -> height, slope_x, slope_z, ground_type
        const ScriptGround g = host.ground(a.q12(0), a.q12(1));
        out[0] = g.height, out[1] = g.slope_x, out[2] = g.slope_z, out[3] = g.ground_type;
        return 4;
    }
    if (fn == "ray") {
        // ray(from_x, from_y, from_z, to_x, to_y, to_z[, mode[, exclude_slot]])
        //   -> hit, x, y, z, distance, slot, ground_type, angle
        const Vec3 from = point(a, 0);
        const Vec3 to = point(a, 3);
        const char* mode = a.text(6);
        const std::string_view m = mode != nullptr ? mode : "solid";
        ScriptRay kind;
        if (m == "solid") {
            kind = ScriptRay::Solid;
        } else if (m == "chara") {
            kind = ScriptRay::Chara;
        } else if (m == "sight") {
            kind = ScriptRay::Sight;
        } else if (m == "probe") {
            kind = ScriptRay::Probe;
        } else {
            return fail(err, err_cap, "mode must be solid, chara, sight or probe");
        }
        const ScriptRayHit hit = host.ray(kind, from, to, a.integer(7, -1));
        out[0] = hit.hit ? 1 : 0;
        out[1] = hit.point.x, out[2] = hit.point.y, out[3] = hit.point.z;
        out[4] = hit.distance;
        out[5] = hit.slot;
        out[6] = hit.ground_type;
        out[7] = hit.angle;
        return 8;
    }
    if (fn == "cell") {
        // cell(x, z) -> has_collision, cell_x, cell_z
        const std::int32_t x = a.q12(0);
        const std::int32_t z = a.q12(1);
        out[0] = host.cell_has_collision(x, z) ? 1 : 0;
        out[1] = floor_to_step(x >> 4, kCellSizeQ8);
        out[2] = floor_to_step(z >> 4, kCellSizeQ8);
        return 3;
    }
    if (fn == "collision_groups") {
        // collision_groups() -> the active group mask (bit 0: map geometry, others: switchable parts)
        out[0] = host.collision_groups();
        return 1;
    }
    if (fn == "speed_zone") {
        // speed_zone(x, z) -> 0 slow, 1 normal, 2 fast
        out[0] = host.speed_zone(a.q12(0), a.q12(1));
        return 1;
    }
    // ray_surface(from_x, from_y, from_z, to_x, to_y, to_z) -> hit, distance, ground_type, angle, ground_height:
    // the solid ray with the surface it met.
    const ScriptRayHit hit = host.ray(ScriptRay::Solid, point(a, 0), point(a, 3), -1);
    out[0] = hit.hit ? 1 : 0;
    out[1] = hit.distance, out[2] = hit.ground_type, out[3] = hit.angle, out[4] = hit.ground_height;
    return 5;
}

// The binding of the C call chain.
WorldScriptHost* s_host = nullptr;
decltype(ShScriptGameApi::named_call) s_next_call = nullptr;
decltype(ShScriptGameApi::named_list) s_next_list = nullptr;

int chained_call(const char* name, int argc, const ShScriptArg* argv, long long* out, int out_cap, char* err,
                 int err_cap) {
    if (s_host != nullptr) {
        const int n = world_script_call(*s_host, name, argc, argv, out, out_cap, err, err_cap);
        if (n != kNotWorldCall) {
            return n;
        }
    }
    return s_next_call != nullptr ? s_next_call(name, argc, argv, out, out_cap, err, err_cap)
                                  : fail(err, err_cap, "not available");
}

const char* const* chained_list() {
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

int world_script_call(WorldScriptHost& host, const char* name, int argc, const ShScriptArg* argv, long long* out,
                      int out_cap, char* err, int err_cap) {
    if (name == nullptr || std::strncmp(name, "world.", 6) != 0) {
        return kNotWorldCall;
    }
    const std::string_view fn(name + 6);
    if (!is_world_name(fn)) {
        return kNotWorldCall;
    }
    if (out_cap < kAnswerMax) {
        return fail(err, err_cap, "output too small");
    }
    return call(host, fn, Args{argc, argv}, out, err, err_cap);
}

const char* const* world_script_names() {
    static const char* const names[] = {kNames[0], kNames[1], kNames[2], kNames[3], kNames[4], kNames[5], nullptr};
    return names;
}

void bind_world_script(ShScriptGameApi& api, WorldScriptHost& host) {
    s_host = &host;
    s_next_call = api.named_call;
    s_next_list = api.named_list;
    api.named_call = chained_call;
    api.named_list = chained_list;
}

}  // namespace sh::world
