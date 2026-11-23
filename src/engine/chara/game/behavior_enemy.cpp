// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Enemy calls of the behaviour API the combat calls do not serve: whether any NPC is busy, the game's index of a
// character, whether a box is on screen, the Q12 square root, the character's step sound on a floor (its footsteps
// table in the character data), a sound at a world point, the turn of a bone in its parent's space and the map's
// pinning point of an attacker that has Harry on the ground.

#include <algorithm>
#include <cstdint>
#include <string_view>

#include "engine/chara/behavior_host.h"
#include "engine/chara/game/bone_ops.h"
#include "engine/game_state/session.h"
#include "engine/chara/game/chara_entry.h"
#include "engine/chara/game/chara_link.h"
#include "engine/combat/combat_math.h"
#include "engine/chara/game/chara_records.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/script/named_api_calls.h"

extern "C" {
// Positional sounds (src/audio/game/placed_sound.cpp).
void Sfx_WithFlagsPlay(int sfx, const void* position, std::int32_t volume, std::int32_t flags);
}  // extern "C"

namespace {

namespace cg = sh::chara::game;
namespace ent = sh::entities;
using sh::chara::behavior::Args;
using sh::chara::behavior::fail;

const char* const kNames[] = {
    "entity.any_npc_active", "entity.npc_index",     "entity.aabb_visible", "entity.sqrt_q12",
    "entity.footstep_sound", "entity.sfx_flags_at",  "entity.bone_turn",    "entity.pin_point",
};

std::int32_t i32(const Args& a, int i) { return static_cast<std::int32_t>(a.integer(i)); }

int call(std::string_view fn, const Args& a, long long* out, char* err, int errCap) {
    if (fn == "any_npc_active") {
        // Any enemy group has an NPC slot.
        const auto& groups = sh::game::session().system.group_npcs;
        const auto used = [](std::int32_t slot) { return slot != -1; };
        out[0] = std::any_of(std::begin(groups), std::end(groups), used) ? 1 : 0;
        return 1;
    }
    if (fn == "aabb_visible") {
        // aabb_visible(min_x, max_x, min_y, max_y, min_z, max_z): the box (geometry units) is on screen.
        out[0] = Vw_AabbVisibleInScreenCheck(i32(a, 0), i32(a, 1), i32(a, 2), i32(a, 3), i32(a, 4), i32(a, 5)) != 0 ? 1 : 0;
        return 1;
    }
    if (fn == "sqrt_q12") {
        out[0] = sh::combat::sqrt_q12(i32(a, 0));
        return 1;
    }
    if (fn == "sfx_flags_at") {
        // sfx_flags_at(sfx, x, y, z, vol_q8, flags): sfx_flags at a world point instead of the character.
        const ent::Vec3 pos{i32(a, 1), i32(a, 2), i32(a, 3)};
        Sfx_WithFlagsPlay(i32(a, 0), &pos, i32(a, 4), i32(a, 5));
        return 0;
    }
    if (fn == "pin_point") {
        // pin_point(x, z, angle): where an attacker that pins Harry down at (x, z) facing `angle` ends up,
        // Harry's own collision taken into account (the map's player code; maps without one leave it).
        std::int32_t x = i32(a, 0);
        std::int32_t z = i32(a, 1);
        auto angle = static_cast<std::int16_t>(a.integer(2));
        cg::link::player_pin_point(x, z, angle);
        out[0] = x, out[1] = z;
        return 2;
    }
    const int slot = static_cast<int>(a.integer(0, -1));
    ent::CharacterRecord* r = cg::record(slot);
    if (r == nullptr) {
        return fail(err, errCap, "bad character slot");
    }
    if (fn == "footstep_sound") {
        // footstep_sound(slot, ground_type, fallback): the step sound of the character on that floor; no
        // pitch jitter is drawn (entity.footstep_sfx draws it for the walkers that use it).
        out[0] = Pc_CharaFootstepSfx(r->model.chara_id, static_cast<int>(a.integer(1)), static_cast<int>(a.integer(2)));
        return 1;
    }
    if (fn == "npc_index") {
        out[0] = PcCombat_NpcIndexOf(reinterpret_cast<const _SubCharacter*>(r));
        return 1;
    }
    if (fn == "bone_turn") {
        // bone_turn(slot, bone, rx, ry, rz): the bone's local rotation turned by the angles (order ZXY) in its
        // parent's space, R * M (entity.bone_rotate turns it in its own space, M * R).
        const cg::link::Skeleton sk = cg::link::skeleton(slot);
        const int bone = static_cast<int>(a.integer(1, -1));
        if (sk.bones == nullptr || sk.animation == nullptr || bone < 0 || bone >= sk.bone_count) {
            return fail(err, errCap, "bone out of range");
        }
        cg::bones::turn_parent(sk.bones[bone],
                               sh::math::rot_matrix_zxy_neg({static_cast<std::int16_t>(a.integer(2)),
                                                            static_cast<std::int16_t>(a.integer(3)),
                                                            static_cast<std::int16_t>(a.integer(4))}));
        return 0;
    }
    return fail(err, errCap, "unknown function");
}

sh::chara::behavior::Extension s_extension = {
    nullptr, 0, kNames, sizeof(kNames) / sizeof(kNames[0]), call, nullptr, 0, nullptr,
};

const sh::chara::behavior::Registrar s_registrar{&s_extension};

}  // namespace
