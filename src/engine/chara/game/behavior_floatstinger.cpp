// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The Floatstinger part of the behaviour API (gamedata/chara/Floatstinger/behavior.lua, the moth boss of map4_s05):
// the state every Floatstinger shares for the whole process (the wing-flap speed and the trail of Harry's positions
// keep their values across map loads, like the game's variables), the acid spit the map draws, and two generic calls
// (a wall offset without a shared result block, a ray that stops at characters).

#include <cstdint>
#include <cstring>
#include <string_view>

#include "engine/chara/behavior_host.h"
#include "engine/chara/game/chara_link.h"
#include "engine/chara/game/chara_records.h"
#include "engine/chara/game/game_records.h"
#include "engine/world/game/character_world.h"

extern "C" {
// The acid drops (src/engine/effects/game/particle_host.cpp).
void Acid_Spit(void* from, void* to);
}  // extern "C"

namespace {

namespace cg = sh::chara::game;
namespace wg = sh::world::game;
namespace ent = sh::entities;
using sh::chara::behavior::Args;
using sh::chara::behavior::fail;

const char* const kNames[] = {
    "entity.floatstinger_flap_speed", "entity.floatstinger_set_flap_speed", "entity.floatstinger_trail_push",
    "entity.floatstinger_trail", "entity.floatstinger_acid",
    // generic
    "entity.wall_offset", "entity.ray_chara_blocked",
};

// The wing-flap speed of the hover clip (keyframes per second, Q12).
std::int32_t s_flap_speed = 0;
// Harry's position on the last eight fighting ticks, newest first.
constexpr int kTrail = 8;
ent::Vec3 s_trail[kTrail] = {};

std::int32_t i32(const Args& a, int i) { return static_cast<std::int32_t>(a.integer(i)); }

ent::Vec3 vec(const Args& a, int i) { return {i32(a, i), i32(a, i + 1), i32(a, i + 2)}; }

int call(std::string_view fn, const Args& a, long long* out, char* err, int errCap) {
    if (fn == "floatstinger_flap_speed") {
        out[0] = s_flap_speed;
        return 1;
    }
    if (fn == "floatstinger_set_flap_speed") {
        s_flap_speed = i32(a, 0);
        return 0;
    }
    if (fn == "floatstinger_trail_push") {
        // trail_push(x, y, z): the oldest entry drops off.
        std::memmove(&s_trail[1], &s_trail[0], sizeof(ent::Vec3) * (kTrail - 1));
        s_trail[0] = vec(a, 0);
        return 0;
    }
    if (fn == "floatstinger_trail") {
        // trail(i): entry i (0 newest).
        const long long i = a.integer(0, -1);
        if (i < 0 || i >= kTrail) {
            return fail(err, errCap, "trail entry out of range");
        }
        out[0] = s_trail[i].x, out[1] = s_trail[i].y, out[2] = s_trail[i].z;
        return 3;
    }
    if (fn == "floatstinger_acid") {
        // acid(x0, y0, z0, x1, y1, z1): an acid drop flying from the first point at the second (Q12). The
        // boss maps draw it (their header has the acid drawer); elsewhere (a debug spawn) the spit has no drop.
        if (cg::link::map_has_acid_drawer()) {
            ent::Vec3 from = vec(a, 0);
            ent::Vec3 to = vec(a, 3);
            Acid_Spit(&from, &to);
        }
        return 0;
    }
    ent::CharacterRecord* r = cg::record(static_cast<int>(a.integer(0, -1)));
    if (r == nullptr) {
        return fail(err, errCap, "bad character slot");
    }
    if (fn == "wall_offset") {
        // wall_offset(slot, dx, dy, dz): the move offset the walls let the character make (Q12).
        cg::WallResult result = {};
        const ent::Vec3 move = vec(a, 1);
        wg::move_character(cg::world_record(result), cg::world_vec(move), *r);
        out[0] = result.offset.x, out[1] = result.offset.y, out[2] = result.offset.z;
        return 3;
    }
    if (fn == "ray_chara_blocked") {
        // ray_chara_blocked(slot, x, y, z, dx, dy, dz): 1 when the ray from (x, y, z) along (dx, dy, dz) meets
        // another character (the slot's own is skipped).
        cg::RayRecord trace = {};
        const ent::Vec3 from = vec(a, 1);
        ent::Vec3 offset = vec(a, 4);
        out[0] = wg::trace(cg::world_record(trace),
                           cg::world_vec(from), cg::world_vec(offset), wg::TraceKind::Chara, r) ? 1 : 0;
        return 1;
    }
    return sh::chara::behavior::kNotMine;
}

sh::chara::behavior::Extension s_extension = {
    nullptr, 0, kNames, sizeof(kNames) / sizeof(kNames[0]), call, nullptr, 0, nullptr,
};

const sh::chara::behavior::Registrar s_registrar{&s_extension};

}  // namespace
