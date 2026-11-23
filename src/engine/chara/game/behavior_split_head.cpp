// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The Split Head part of the behaviour API (gamedata/chara/SplitHead/behavior.lua, the school boss of
// map1_s05/map1_s06): the move offset Harry's code hands it, the blood the map draws for it and the push on Harry.

#include <cstdint>
#include <string_view>

#include "engine/chara/behavior_host.h"
#include "engine/chara/game/chara_entry.h"
#include "engine/chara/game/chara_link.h"
#include "engine/chara/game/chara_records.h"
#include "engine/maps/map_runtime.h"
#include "engine/player/player.h"
#include "engine/script/sh_script.h"
#include "engine/chara/game/game_records.h"
#include "engine/world/game/character_world.h"

namespace {

namespace cg = sh::chara::game;
namespace wg = sh::world::game;
namespace ent = sh::entities;
using sh::chara::behavior::Args;
using sh::chara::behavior::fail;

const char* const kNames[] = {
    "entity.split_head_move_offset", "entity.split_head_blood_drop", "entity.split_head_blood_strand",
    "entity.split_head_push_player",
    // generic
    "entity.player_damage_disable", "entity.ray_hit_chara",
};

// The move offset Harry's collision gives the boss this tick (Q12, wraps as 16 bits).
std::int16_t s_move_offset_x = 0;
std::int16_t s_move_offset_z = 0;
// A Split Head off its maps pushed Harry since his last tick.
bool s_foreign_push = false;

// The boss's maps: their overlay.json draws the Split Head's effects ("effect_draws": "split_head").
bool on_boss_map() { return sh::maps::map_runtime().draws(sh::maps::MapEffect::SplitHead); }

std::int32_t i32(const Args& a, int i) { return static_cast<std::int32_t>(a.integer(i)); }

int call(std::string_view fn, const Args& a, long long* out, char* err, int errCap) {
    if (fn == "split_head_move_offset") {
        // move_offset(slot): the offset gathered since the last call, then cleared.
        out[0] = s_move_offset_x, out[1] = s_move_offset_z;
        s_move_offset_x = 0;
        s_move_offset_z = 0;
        return 2;
    }
    if (fn == "split_head_push_player" && !on_boss_map()) {
        // Off its maps (an engine spawn) the body still shoves Harry: his tick takes the push as the boss map's
        // pushing floor does (Pc_SplitHead_PushTake); without it the charge, its only attack before the roar,
        // would pass through him.
        const int slot = i32(a, 0);
        if (slot <= 0 || cg::record(slot) == nullptr) {
            return fail(err, errCap, "bad character slot");
        }
        s_foreign_push = true;
        cg::link::player_push(i32(a, 1), i32(a, 2), ent::Vec3{i32(a, 3), i32(a, 4), i32(a, 5)}, slot);
        return 0;
    }
    if (fn == "split_head_blood_drop" || fn == "split_head_blood_strand" || fn == "split_head_push_player") {
        // The boss map's own effect and push functions; elsewhere (an engine spawn) no map draws the blood.
        if (!on_boss_map()) {
            return 0;
        }
        if (fn == "split_head_blood_drop") {
            // blood_drop(slot, x, y, z, speed, heading): a drop from the point (Q8) flying along heading.
            cg::link::map_blood_drop(i32(a, 1), i32(a, 2), i32(a, 3), i32(a, 4), i32(a, 5));
            return 0;
        }
        if (fn == "split_head_blood_strand") {
            // blood_strand(slot, kind, alpha, x0, y0, z0, x1, y1, z1): a strand between two points (Q8).
            cg::link::map_blood_strand(i32(a, 1), i32(a, 2), i32(a, 3), i32(a, 4), i32(a, 5), i32(a, 6), i32(a, 7),
                                       i32(a, 8));
            return 0;
        }
        // push_player(slot, x, z, vx, vy, vz): Harry pushed by (x, z) (Q12) with the knock vector.
        cg::link::player_push(i32(a, 1), i32(a, 2), ent::Vec3{i32(a, 3), i32(a, 4), i32(a, 5)});
        return 0;
    }
    if (fn == "player_damage_disable") {
        // player_damage_disable(disable): Harry takes no damage while set; returns whether he is dead.
        out[0] = sh::game::player().dead() ? 1 : 0;
        sh::game::player().set_damage_disabled(static_cast<std::uint8_t>(a.integer(0)) != 0);
        return 1;
    }
    if (fn == "ray_hit_chara") {
        // ray_hit_chara(slot, x, y, z, dx, dy, dz): like ray_los_hit; hit, and 1 when what it hit is a
        // character.
        ent::CharacterRecord* excluded = a.integer(0) > 0 ? cg::record(static_cast<int>(a.integer(0))) : nullptr;
        const ent::Vec3 from{i32(a, 1), i32(a, 2), i32(a, 3)};
        const ent::Vec3 offset{i32(a, 4), i32(a, 5), i32(a, 6)};
        cg::RayRecord trace = {};
        out[0] = wg::trace(cg::world_record(trace),
                           cg::world_vec(from), cg::world_vec(offset), wg::TraceKind::Sight, excluded) ? 1 : 0;
        out[1] = trace.character != nullptr ? 1 : 0;
        return 2;
    }
    if (cg::record(static_cast<int>(a.integer(0, -1))) == nullptr) {
        return fail(err, errCap, "bad character slot");
    }
    return sh::chara::behavior::kNotMine;
}

sh::chara::behavior::Extension s_extension = {
    nullptr, 0, kNames, sizeof(kNames) / sizeof(kNames[0]), call, nullptr, 0, nullptr,
};

const sh::chara::behavior::Registrar s_registrar{&s_extension};

}  // namespace

extern "C" void Pc_SplitHead_MoveOffsetAdd(int x, int z) {
    s_move_offset_x = static_cast<std::int16_t>(s_move_offset_x + x);
    s_move_offset_z = static_cast<std::int16_t>(s_move_offset_z + z);
}

extern "C" int Pc_SplitHead_PushTake(void) {
    const bool pushed = s_foreign_push;
    s_foreign_push = false;
    return pushed ? 1 : 0;
}
