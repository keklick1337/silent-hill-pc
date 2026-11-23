// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The behaviour host's base extension and ports over the session's character slots: the record fields every behaviour reads and writes by name, the clip tables from the character data,
// the world calls of sh.entity.* (ground, wall test, sounds at a record, effects, bones, clip playback, the overlay's
// shared variables), the spawn ports, and the update entries the map headers hold for types whose behaviour is Lua.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>

#include "engine/chara/behavior_host.h"
#include "engine/chara/game/bone_ops.h"
#include "engine/chara/game/chara_clips.h"
#include "engine/chara/game/chara_entry.h"
#include "engine/chara/game/chara_link.h"
#include "engine/chara/game/chara_records.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/chara/runtime/chara_models.h"
#include "engine/maps/map_runtime.h"
#include "engine/chara/runtime/chara_spawn.h"
#include "engine/entities/game/entity_host.h"
#include "engine/game_state/game_ids.h"
#include "engine/player/player.h"
#include "engine/script/sh_script.h"
#include "engine/chara/game/game_records.h"
#include "engine/chara/game/harry_reaction_import.h"
#include "audio/game/game_sound_driver.h"
#include "engine/world/game/character_world.h"

extern "C" {
// Combat effects and the clip colour over records (src/engine/effects/game/combat_effects_host.cpp).
void Chara_EffectSpawn(void* chara, const void* position, std::int32_t strength, std::int32_t kind);
void Combat_WeaponEffectSpawn(std::int32_t attack, std::int16_t rot_y, std::int16_t rot_x,
                              sh::entities::BoneCoord* bone);
// The player's footsteps (src/engine/entities/game/player_host.cpp).
void Player_FootstepSfxGetFor(std::int32_t chara_id, std::int8_t ground, std::int32_t* sfx, std::int8_t* pitch0,
                              std::int8_t* pitch1);
// Positional sounds (src/audio/game/placed_sound.cpp).
void Sfx_WithFlagsPlay(int sfx, const void* position, std::int32_t volume, std::int32_t flags);
void Sfx_WithPitchPlay(int sfx, const void* position, std::int32_t volume, std::int8_t pitch);
}  // extern "C"

namespace behavior = sh::chara::behavior;
namespace cg = sh::chara::game;
namespace wg = sh::world::game;
namespace ent = sh::entities;
namespace ids = sh::game_state::chara;

namespace {

using behavior::Args;
using behavior::fail;
using behavior::Field;
using behavior::Kind;

constexpr std::size_t kVars = offsetof(ent::CharacterRecord, vars);

#define F(name, member, kind) \
    {name, offsetof(ent::CharacterRecord, member), SH_FIELD_KIND(ent::CharacterRecord, member, kind)}
#define V(name, offset, kind) {name, kVars + (offset), Kind::kind}

// The record fields every behaviour reads and writes. "npc_*" is the properties block of the cutscene NPCs.
const Field kFields[] = {
    F("chara_id", model.chara_id, S32),
    F("palette", model.palette, U8),
    F("control_state", model.control_state, U8),
    F("state_step", model.control_step, U8),
    F("anim_status", model.anim.status, U8),
    F("anim_flags", model.anim.flags, U16),
    F("anim_time", model.anim.time, S32),
    F("anim_keyframe", model.anim.keyframe, S16),
    F("anim_alpha", model.anim.alpha, S16),
    F("pos_x", position.x, S32),
    F("pos_y", position.y, S32),
    F("pos_z", position.z, S32),
    F("rot_x", rotation.x, S16),
    F("rot_y", rotation.y, S16),
    F("rot_z", rotation.z, S16),
    F("angle_to_target", angle_to_target, S16),
    F("fall_speed", fall_speed, S32),
    F("move_speed", move_speed, S32),
    F("heading_angle", heading, S16),
    F("flags", flags, S16),
    F("attack_received", attack_received, S32),
    F("health", health, S32),
    F("death_timer", death_timer, U16),
    F("aux_timer", timer, S16),  // a general timer each type uses its own way
    F("box_top", collision.box.top, S16),
    F("box_bottom", collision.box.bottom, S16),
    F("box_offset_y", collision.box.offset_y, S16),
    F("cylinder_radius", collision.cylinder.radius, S16),
    F("cylinder_extent", collision.cylinder.chara_radius, S16),
    F("box_offset_x", collision.offsets.box.x, S16),
    F("box_offset_z", collision.offsets.box.z, S16),
    F("cylinder_offset_x", collision.offsets.cylinder.x, S16),
    F("cylinder_offset_z", collision.offsets.cylinder.z, S16),
    // The attack block past the window: the targets the swing hit (-1: none yet), its angles and its point.
    F("attack_hit", attack.hit_mask, S32),
    F("attack_angle_y", attack.aim_yaw, S16),
    F("attack_angle_x", attack.aim_pitch, S16),
    F("attack_x", attack.origin.x, S32),
    F("attack_y", attack.origin.y, S32),
    F("attack_z", attack.origin.z, S32),
    V("npc_control_state", cg::npc_vars::kControlState, S32),
    V("npc_freeze", cg::npc_vars::kFreeze, S32),
    V("npc_reset_control_state", cg::npc_vars::kResetControlState, S32),
    V("npc_distance_to_player", cg::npc_vars::kDistanceToPlayer, S32),
    V("npc_flags", cg::npc_vars::kFlags, S32),
    // Kaufmann's shot at Dahlia happened (one word over the two halves of that slot).
    V("npc_shot_fired", cg::npc_vars::kShotFired, S32),
    V("npc_move_distance", cg::npc_vars::kMoveDistance, S16),
    V("npc_move_speed", cg::npc_vars::kMoveSpeed, S16),
    // Harry's block; the collision reset clears its first words on every NPC too.
    V("player_afk_timer", cg::player_vars::kAfkTimer, S32),
    V("player_ground_height", cg::player_vars::kGroundHeight, S32),
    V("player_flex_angle_x", cg::player_vars::kFlexAngleX, S32),
    V("player_heading_angle", cg::player_vars::kHeadingAngle, S16),
    V("cat_meow_toggle", 0, U8),
};

#undef V
#undef F

const char* const kNames[] = {
    "entity.collision_state", "entity.set_collision_state", "entity.anim_step", "entity.clip_start",
    "entity.clip_variable", "entity.clip_duration_set", "entity.bone_translation_set", "entity.bone_rotation_set",
    "entity.bone_flag_set", "entity.bone_scale", "entity.ground_at", "entity.footstep_sfx", "entity.wall_detect",
    "entity.sfx_at", "entity.sfx_pitched", "entity.sfx_flags", "entity.player_control_disabled",
    "entity.weapon_effect", "entity.effect_spawn", "entity.map_var", "entity.set_map_var", "entity.clip_end",
};

// Overlay variables a behaviour shares with the overlay's own code (by readable name).
struct MapVar {
    const char* name;
    cg::link::OverlayVariable which;
    int bytes;  // 1: u8, 4: s32
};

const MapVar kMapVars[] = {
    // Dahlia's "effect spawned" latch of her control state 19; Monster Cybil's code reads it too.
    {"dahlia_effect_spawned", cg::link::OverlayVariable::DahliaEffectSpawned, 1},
};

// A map without the variable keeps the value here (the original's own copy of the variable).
long long s_map_var_fallback[sizeof(kMapVars) / sizeof(kMapVars[0])] = {};

int map_var(std::string_view fn, const Args& a, long long* out, char* err, int errCap) {
    const char* name = a.text(0);
    for (std::size_t i = 0; name != nullptr && i < sizeof(kMapVars) / sizeof(kMapVars[0]); ++i) {
        const MapVar& v = kMapVars[i];
        if (std::strcmp(v.name, name) != 0) {
            continue;
        }
        void* p = cg::link::overlay_variable(v.which);
        if (fn == "map_var") {
            if (p == nullptr) {
                out[0] = s_map_var_fallback[i];
            } else if (v.bytes == 1) {
                out[0] = *static_cast<std::uint8_t*>(p);
            } else {
                std::int32_t x;
                std::memcpy(&x, p, 4);
                out[0] = x;
            }
            return 1;
        }
        const long long value = a.integer(1);
        if (p == nullptr) {
            s_map_var_fallback[i] = v.bytes == 1 ? static_cast<std::uint8_t>(value) : static_cast<std::int32_t>(value);
        } else if (v.bytes == 1) {
            *static_cast<std::uint8_t*>(p) = static_cast<std::uint8_t>(value);
        } else {
            const auto x = static_cast<std::int32_t>(value);
            std::memcpy(p, &x, 4);
        }
        return 0;
    }
    return fail(err, errCap, "unknown map variable '%s'", name != nullptr ? name : "?");
}

int clip_call(std::string_view fn, const Args& a, ent::CharacterRecord* r, int slot, long long* out, char* err,
              int errCap) {
    const cg::ClipTableRef active = cg::active_clip_table(r->model.chara_id, r->model.anim.clip_table);
    if (active.clips == nullptr) {
        return fail(err, errCap, "the character has no clip table");
    }
    if (fn == "anim_step") {
        const cg::link::Skeleton sk = cg::link::skeleton(slot);
        if (r->model.anim.status >= active.count) {
            out[0] = 0;
            return 1;
        }
        cg::ClipInfo* info = &active.clips[r->model.anim.status];
        if (info->playback == nullptr || sk.bones == nullptr || sk.animation == nullptr) {
            out[0] = 0;
            return 1;
        }
        info->playback(&r->model, sk.animation, sk.bones, info);
        out[0] = 1;
        return 1;
    }
    const long long status = a.integer(1, -1);
    if (status < 0 || status >= active.count) {
        return fail(err, errCap, "clip status out of range");
    }
    cg::ClipInfo& clip = active.clips[status];
    if (fn == "clip_start") {
        out[0] = clip.start_keyframe;
        return 1;
    }
    if (fn == "clip_end") {
        out[0] = clip.end_keyframe;
        return 1;
    }
    if (fn == "clip_variable") {
        // 1 when the clip's speed comes from a function its own behaviour installs (only it may play the clip).
        out[0] = clip.variable_speed != 0 ? 1 : 0;
        return 1;
    }
    clip.speed = static_cast<std::int32_t>(a.integer(2));
    return 0;
}

int bone_call(std::string_view fn, const Args& a, int slot, char* err, int errCap) {
    const cg::link::Skeleton sk = cg::link::skeleton(slot);
    const int bone = static_cast<int>(a.integer(1, -1));
    if (sk.bones == nullptr || sk.animation == nullptr || bone < 0 || bone >= sk.bone_count) {
        return fail(err, errCap, "bone out of range");
    }
    ent::BoneCoord& b = sk.bones[bone];
    if (fn == "bone_translation_set") {
        b.local.t[0] = static_cast<std::int32_t>(a.integer(2));
        b.local.t[1] = static_cast<std::int32_t>(a.integer(3));
        b.local.t[2] = static_cast<std::int32_t>(a.integer(4));
        return 0;
    }
    if (fn == "bone_rotation_set") {
        cg::bones::rotation_set(b, static_cast<std::int16_t>(a.integer(2)), static_cast<std::int16_t>(a.integer(3)),
                                static_cast<std::int16_t>(a.integer(4)));
        return 0;
    }
    if (fn == "bone_flag_set") {
        b.world_current = static_cast<std::uint32_t>(static_cast<std::int32_t>(a.integer(2)));
        return 0;
    }
    if (fn == "bone_scale") {
        cg::bones::scale_columns(sk.bones, bone, static_cast<std::int32_t>(a.integer(2)),
                                 static_cast<std::int32_t>(a.integer(3)), static_cast<std::int32_t>(a.integer(4)));
        return 0;
    }
    return fail(err, errCap, "unknown function");
}

int world_call(std::string_view fn, const Args& a, long long* out, char* err, int errCap) {
    if (fn == "ground_at") {
        cg::GroundSurface surface{};
        cg::world_record(surface) = wg::surface_at(static_cast<std::int32_t>(a.integer(0)),
                                                   static_cast<std::int32_t>(a.integer(1)));
        out[0] = surface.height, out[1] = surface.kind;
        return 2;
    }
    if (fn == "footstep_sfx") {
        // footstep_sfx(ground_type[, chara_id]): the character's footsteps table (the player's by default).
        std::int32_t sfx = 0;
        std::int8_t pitch0 = 0, pitch1 = 0;
        Player_FootstepSfxGetFor(static_cast<std::int32_t>(a.integer(1, sh::chara::kPlayerChara)),
                                 static_cast<std::int8_t>(a.integer(0)), &sfx, &pitch0, &pitch1);
        out[0] = sfx, out[1] = pitch0, out[2] = pitch1;
        return 3;
    }
    if (fn == "player_control_disabled") {
        out[0] = sh::game::player().get(sh::game::PlayerVar::ControlDisabled);
        return 1;
    }
    if (fn == "weapon_effect") {
        // weapon_effect(weapon_attack, rot_y, rot_x[, bone]): a hit effect at a bone of the shared NPC bone buffer.
        const long long bone = a.integer(3, 0);
        if (bone < 0 || bone >= ent::kNpcBonePoolSize) {
            return fail(err, errCap, "bone out of range");
        }
        Combat_WeaponEffectSpawn(static_cast<std::int32_t>(a.integer(0)), static_cast<std::int16_t>(a.integer(1)),
                                 static_cast<std::int16_t>(a.integer(2)), &cg::slots().npc_bone_pool[bone]);
        return 0;
    }
    if (fn == "map_var" || fn == "set_map_var") {
        return map_var(fn, a, out, err, errCap);
    }
    const int slot = static_cast<int>(a.integer(0, -1));
    ent::CharacterRecord* r = cg::record(slot);
    if (r == nullptr) {
        return fail(err, errCap, "bad character slot");
    }
    if (fn == "collision_state") {
        out[0] = r->collision.state();
        return 1;
    }
    if (fn == "set_collision_state") {
        r->collision.set_state(static_cast<int>(a.integer(1)));
        return 0;
    }
    if (fn == "sfx_at" || fn == "sfx_pitched") {
        Sfx_WithPitchPlay(static_cast<int>(a.integer(1)), &r->position, static_cast<std::int32_t>(a.integer(2)),
                          static_cast<std::int8_t>(a.integer(3)));
        return 0;
    }
    if (fn == "sfx_flags") {
        Sfx_WithFlagsPlay(static_cast<int>(a.integer(1)), &r->position, static_cast<std::int32_t>(a.integer(2)),
                          static_cast<std::int32_t>(a.integer(3)));
        return 0;
    }
    if (fn == "effect_spawn") {
        // effect_spawn(slot, x, y, z, kind, variant): an effect at a world point (Q12).
        const std::int32_t pos[4] = {static_cast<std::int32_t>(a.integer(1)), static_cast<std::int32_t>(a.integer(2)),
                                     static_cast<std::int32_t>(a.integer(3)), 0};
        Chara_EffectSpawn(r, pos, static_cast<std::int32_t>(a.integer(4)), static_cast<std::int32_t>(a.integer(5)));
        return 0;
    }
    if (fn == "wall_detect") {
        // The overlay's own result block when the map has one (other overlay code reads it back).
        static cg::WallResult local;
        auto* result = static_cast<cg::WallResult*>(cg::link::overlay_variable(cg::link::OverlayVariable::WallResult));
        if (result == nullptr) {
            result = &local;
        }
        const ent::Vec3 offset{static_cast<std::int32_t>(a.integer(1)), static_cast<std::int32_t>(a.integer(2)),
                               static_cast<std::int32_t>(a.integer(3))};
        wg::move_character(cg::world_record(*result), cg::world_vec(offset), *r);
        out[0] = result->offset.x, out[1] = result->offset.y, out[2] = result->offset.z;
        out[3] = result->surface.height, out[4] = result->surface.kind;
        return 5;
    }
    if (fn == "anim_step" || fn == "clip_start" || fn == "clip_duration_set" || fn == "clip_end" ||
        fn == "clip_variable") {
        return clip_call(fn, a, r, slot, out, err, errCap);
    }
    return bone_call(fn, a, slot, err, errCap);
}

std::uint8_t* record_bytes(int slot) { return reinterpret_cast<std::uint8_t*>(cg::record(slot)); }
void record_commit(int slot) {
    if (slot == -1) {
        cg::commit_unset_slot();
    }
}

int record_type(int slot) {
    const ent::CharacterRecord* r = cg::record(slot);
    return r != nullptr ? r->model.chara_id : -1;
}

behavior::Extension s_base = {
    kFields, sizeof(kFields) / sizeof(kFields[0]), kNames, sizeof(kNames) / sizeof(kNames[0]), world_call,
    nullptr, 0, nullptr,
};

struct Install {
    Install() {
        behavior::register_base_extension(&s_base);
        behavior::Ports ports;
        ports.record = record_bytes;
        ports.commit = record_commit;
        ports.chara_id = record_type;
        ports.var_get = sh::entities::game::var_get;
        ports.var_set = sh::entities::game::var_set;
        ports.library = sh::chara::game_library;
        ports.clip_table = cg::data_clip_table;
        behavior::install_ports(ports);
        sh::chara::SpawnPorts spawn;
        spawn.pool_ensure = [](int chara_id) { return Pc_CharaPool_Ensure(chara_id) != 0; };
        spawn.set_spawn_run = [](int slot, int run) { g_PcNpcSpawnAi[slot - 1] = static_cast<unsigned char>(run); };
        spawn.harry_reactions_ensure = [](int chara_id, bool hostile) {
            Pc_HarryReactions_EnsureFor(chara_id, hostile ? 1 : 0);
        };
        spawn.sound_banks_ensure = [](int chara_id) { Pc_SoundBanks_EnsureFor(chara_id); };
        sh::chara::install_spawn_ports(spawn);
    }
};
const Install s_install;

int npc_slot(const _SubCharacter* npc) { return cg::slot_of(npc); }

}  // namespace

extern "C" {

void Pc_CharaBehavior_Scripted(_SubCharacter* npc, _AnmHeader*, _GsCOORDINATE2*) {
    behavior::run_behaviour(npc_slot(npc), reinterpret_cast<ent::CharacterRecord*>(npc)->model.chara_id);
}

void Pc_CharaBehavior_ScriptedAlt(_SubCharacter* npc, _AnmHeader*, _GsCOORDINATE2*) {
    behavior::run_behaviour(npc_slot(npc), reinterpret_cast<ent::CharacterRecord*>(npc)->model.chara_id, 1);
}

void Pc_CharaBehavior_ScriptedAs(_SubCharacter* npc, _AnmHeader*, _GsCOORDINATE2*) {
    const int type = reinterpret_cast<ent::CharacterRecord*>(npc)->model.chara_id;
    const int as = sh::maps::map_runtime().chara_update_as(type);
    behavior::run_behaviour(npc_slot(npc), as >= 0 ? as : type);
}

void Pc_CharaBehavior_Fallback(_SubCharacter* npc, _AnmHeader*, _GsCOORDINATE2*) {
    behavior::run_fallback(npc_slot(npc));
}

Pc_CharaUpdateFn Pc_CharaBehavior_ForSlot(int npcIdx) {
    const int chara_id = cg::slots().npcs[npcIdx].model.chara_id;
    const bool valid = chara_id > ids::kNone && chara_id < ids::kMaxTypes;
    const Pc_CharaUpdateFn map_fn = valid ? cg::link::map_chara_update(chara_id) : nullptr;
    switch (behavior::choose_update(chara_id, map_fn != nullptr, g_PcNpcDebugSpawned[npcIdx] != 0,
                                    static_cast<behavior::SpawnRun>(g_PcNpcSpawnAi[npcIdx]))) {
        case behavior::SlotUpdate::Map: return map_fn;
        case behavior::SlotUpdate::Behaviour: return Pc_CharaBehavior_Scripted;
        case behavior::SlotUpdate::Fallback: return Pc_CharaBehavior_Fallback;
        case behavior::SlotUpdate::None: break;
    }
    return nullptr;
}

int Pc_CharaBehavior_Run(_SubCharacter* npc, _AnmHeader* anm, _GsCOORDINATE2* bones,
                         void (*update)(_SubCharacter*, _AnmHeader*, _GsCOORDINATE2*)) {
    if (update == nullptr) {
        return 0;
    }
    update(npc, anm, bones);
    return 1;
}

// The script API chains the host into its named calls.
void PcCharaBehavior_Bind(ShScriptGameApi* api) { behavior::bind_script_api(api); }

}  // extern "C"

// Registration order: sh_script.h.
static ShScriptGameBinderEntry s_ScriptBinder(80, PcCharaBehavior_Bind);
