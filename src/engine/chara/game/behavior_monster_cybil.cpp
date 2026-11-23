// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The Monster Cybil part of the behaviour API (gamedata/chara/MonsterCybil/behavior.lua, the possessed Cybil of the
// amusement park carousel, map6_s04).
//
// The boss is posed in two halves: the record's model drives the legs and a second model the torso, arms and head
// (the upper body plays its own clip over the same bones). That second model lives here (the map's fight setup
// resets it too) and is reached by monster_cybil_upper / set_monster_cybil_upper. Its walk cycle has a speed that
// depends on the fight state: the clip table's variable entries point at walk_speed below, which reads the speeds
// the script hands over. The other calls are generic.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "engine/chara/behavior_host.h"
#include "engine/chara/game/chara_clips.h"
#include "engine/chara/game/chara_entry.h"
#include "engine/chara/game/chara_link.h"
#include "engine/chara/game/chara_records.h"
#include "engine/script/sh_script.h"
#include "engine/chara/game/game_records.h"
#include "engine/world/game/character_world.h"

extern "C" {
// The player's held weapon model (src/engine/inventory/game/item_exit_host.cpp).
void Player_WeaponModelStateSet(std::int32_t state, std::int8_t attack);
}  // extern "C"

namespace {

// The upper body model of the boss (the map's fight setup clears its step).
sh::entities::ModelRecord s_upper_body_model;

namespace cg = sh::chara::game;
namespace wg = sh::world::game;
namespace ent = sh::entities;
using sh::chara::behavior::Args;
using sh::chara::behavior::fail;
using sh::chara::behavior::Kind;

const char* const kNames[] = {
    "entity.monster_cybil_upper",
    "entity.set_monster_cybil_upper",
    "entity.monster_cybil_anim_step",
    "entity.monster_cybil_walk_durations",
    // generic (ray_trace and wall_offset come from the Air Screamer and Floatstinger hosts: one name, one call)
    "entity.bone_world_translation",
    "entity.ray_trace_chara",
    "entity.player_state",
    "entity.player_weapon_model_set",
    "entity.weapon_attack_reach",
    "entity.clip_table_set",
};

// The upper body model by readable name.
struct UpperField {
    const char* name;
    std::size_t offset;
    Kind kind;
};

#define U(name, member, kind) {name, offsetof(ent::ModelRecord, member), SH_FIELD_KIND(ent::ModelRecord, member, kind)}

const UpperField kUpper[] = {
    U("control_state", control_state, U8), U("state_step", control_step, U8), U("anim_status", anim.status, U8),
    U("anim_flags", anim.flags, U16),      U("anim_time", anim.time, S32),     U("anim_keyframe", anim.keyframe, S16),
};

#undef U

// The packed status of the walk cycle's playback.
constexpr std::uint8_t kWalkStatus = (1 << 1) | 1;

// Walk cycle speeds (keyframes per second, Q12) by the clip's first keyframe: while she wanders off and otherwise.
// Handed over by the script when the fight starts.
constexpr int kWalkKeyframes = 24;
std::int32_t s_walk_wander[kWalkKeyframes] = {};
std::int32_t s_walk_fight[kWalkKeyframes] = {};

// The slot whose clip monster_cybil_anim_step is stepping (0: none).
int s_step_slot = 0;

// The record holding the fight fields of the boss in `slot`: the first NPC for the map's boss (as the game does), its
// own record for an engine spawn (the script's fight_record fallback).
const ent::CharacterRecord& fight_record(int slot) {
    if (slot >= 1 && slot <= static_cast<int>(ent::kNpcSlotCount) && g_PcNpcDebugSpawned[slot - 1] != 0) {
        if (const ent::CharacterRecord* own = cg::record(slot)) {
            return *own;
        }
    }
    return cg::slots().npcs[0];
}

// The speed of the boss's variable-speed clips, from its fight fields.
std::int32_t walk_speed(ent::ModelRecord*, cg::ClipInfo* info) {
    const ent::CharacterRecord& boss = fight_record(s_step_slot);
    if (info->status != kWalkStatus || cg::var_read<std::int16_t>(boss, cg::npc_vars::kMonsterCybilWalking) == 0) {
        return 0;
    }
    const int k = info->start_keyframe;
    if (k < 0 || k >= kWalkKeyframes) {
        return 0;
    }
    return cg::var_read<std::uint8_t>(boss, cg::npc_vars::kMonsterCybilWander) == 1 ? s_walk_wander[k] : s_walk_fight[k];
}

cg::ClipTableRef boss_clips() {
    cg::ClipTableRef t;
    return cg::type_clip_table(ShScript_CharaBehaviorRunning(), 0, &t) ? t : cg::ClipTableRef{};
}

int upper(std::string_view fn, const Args& a, long long* out, char* err, int errCap) {
    const char* name = a.text(0);
    for (const UpperField& f : kUpper) {
        if (name == nullptr || std::strcmp(f.name, name) != 0) {
            continue;
        }
        auto* p = reinterpret_cast<unsigned char*>(&s_upper_body_model) + f.offset;
        const bool set = fn[0] == 's';
        const long long v = a.integer(1);
        if (set) {
            sh::chara::field_write(f.kind, p, v);
            return 0;
        }
        out[0] = sh::chara::field_read(f.kind, p);
        return 1;
    }
    return fail(err, errCap, "unknown upper body field '%s'", name != nullptr ? name : "?");
}

int call(std::string_view fn, const Args& a, long long* out, char* err, int errCap) {
    if (fn == "monster_cybil_upper" || fn == "set_monster_cybil_upper") {
        // monster_cybil_upper(name) / set_monster_cybil_upper(name, value)
        return upper(fn, a, out, err, errCap);
    }
    if (fn == "monster_cybil_walk_durations") {
        // monster_cybil_walk_durations(keyframe, wander, fight): one row of the walk speeds; the first call
        // also points the clip table's variable-speed entries at them.
        const long long k = a.integer(0, -1);
        if (k < 0 || k >= kWalkKeyframes) {
            return fail(err, errCap, "walk keyframe out of range");
        }
        s_walk_wander[k] = static_cast<std::int32_t>(a.integer(1));
        s_walk_fight[k] = static_cast<std::int32_t>(a.integer(2));
        if (k == 0) {
            const cg::ClipTableRef t = boss_clips();
            for (int i = 0; i < t.count; ++i) {
                if (t.clips[i].variable_speed) {
                    t.clips[i].speed_fn = walk_speed;
                }
            }
        }
        return 0;
    }
    if (fn == "player_state") {
        // Harry's animation state (e_PlayerState).
        out[0] = cg::slots().player_upper.state;
        return 1;
    }
    if (fn == "player_weapon_model_set") {
        // player_weapon_model_set(state, weapon_attack): Harry's held weapon model (-1 puts it away).
        Player_WeaponModelStateSet(static_cast<std::int32_t>(a.integer(0)), static_cast<std::int8_t>(a.integer(1)));
        return 0;
    }
    if (fn == "weapon_attack_reach") {
        // weapon_attack_reach(attack): how far the attack reaches (Q12, weapon attack table).
        const long long i = a.integer(0, -1);
        if (i < 0 || i >= cg::link::attack_count()) {
            return fail(err, errCap, "weapon attack out of range");
        }
        out[0] = cg::link::attack_row(static_cast<int>(i)).reach_q12;
        return 1;
    }
    const int slot = static_cast<int>(a.integer(0, -1));
    ent::CharacterRecord* r = cg::record(slot);
    if (r == nullptr) {
        return fail(err, errCap, "bad character slot");
    }
    if (fn == "ray_trace_chara") {
        // ray_trace_chara(slot, x, y, z, dx, dy, dz): the ray against the world and the characters but the
        // slot's own; hit, and 1 when what it hit is a character.
        const ent::Vec3 from{static_cast<std::int32_t>(a.integer(1)), static_cast<std::int32_t>(a.integer(2)),
                             static_cast<std::int32_t>(a.integer(3))};
        ent::Vec3 offset{static_cast<std::int32_t>(a.integer(4)), static_cast<std::int32_t>(a.integer(5)),
                         static_cast<std::int32_t>(a.integer(6))};
        cg::RayRecord trace = {};
        out[0] = wg::trace(cg::world_record(trace),
                           cg::world_vec(from), cg::world_vec(offset), wg::TraceKind::Chara, r) ? 1 : 0;
        out[1] = trace.character != nullptr ? 1 : 0;
        return 2;
    }
    if (fn == "clip_table_set") {
        // clip_table_set(slot): the record plays from its type's first clip table (only the base table
        // pointer; the map table pointer stays).
        cg::ClipTableRef t;
        if (!cg::type_clip_table(r->model.chara_id, 0, &t)) {
            return fail(err, errCap, "the character has no clip table");
        }
        r->model.anim.clip_table = t.clips;
        return 0;
    }
    const cg::link::Skeleton sk = cg::link::skeleton(slot);
    if (sk.bones == nullptr || sk.animation == nullptr) {
        return fail(err, errCap, "the character has no bones");
    }
    if (fn == "bone_world_translation") {
        // bone_world_translation(slot, bone): the bone's world position as last computed (geometry units).
        const long long bone = a.integer(1, -1);
        if (bone < 0 || bone >= sk.bone_count) {
            return fail(err, errCap, "bone out of range");
        }
        const ent::BoneMatrix& w = sk.bones[bone].world;
        out[0] = w.t[0], out[1] = w.t[1], out[2] = w.t[2];
        return 3;
    }
    if (fn == "monster_cybil_anim_step") {
        // monster_cybil_anim_step(slot, part, bone_mask): one tick of the clip of the legs (part 0, the record's
        // model) or of the upper body (part 1) on the bones of `bone_mask`.
        const long long part = a.integer(1);
        ent::ModelRecord* model = part == 0 ? &r->model : &s_upper_body_model;
        const cg::ClipTableRef t = boss_clips();
        if (model->anim.status >= t.count) {
            return fail(err, errCap, "clip status out of range");
        }
        cg::link::set_active_bones(sk.animation, static_cast<std::uint32_t>(a.integer(2)));
        cg::ClipInfo* info = &t.clips[model->anim.status];
        s_step_slot = static_cast<int>(slot);
        info->playback(model, sk.animation, sk.bones, info);
        s_step_slot = 0;
        return 0;
    }
    return fail(err, errCap, "unknown function");
}

sh::chara::behavior::Extension s_extension = {
    nullptr, 0, kNames, sizeof(kNames) / sizeof(kNames[0]), call, nullptr, 0, nullptr,
};

const sh::chara::behavior::Registrar s_registrar{&s_extension};

}  // namespace
