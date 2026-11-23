// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The game's combat view: sh::combat::SessionCombatView answers from the session's character slots; this
// subclass adds the world queries (rays, wall tests, sight lines, steering), sounds, effects, clip tables, options
// and cheats, and answers the enemy calls of sh.entity.* (sh::entities::EnemyHost). The C entry points serve the
// code that holds records.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "engine/chara/game/bone_ops.h"
#include "engine/chara/game/chara_clips.h"
#include "engine/chara/game/chara_entry.h"
#include "engine/chara/game/chara_link.h"
#include "engine/chara/game/chara_records.h"
#include "engine/chara/game/game_records.h"
#include "engine/chara/chara_library.h"
#include "engine/chara/runtime/chara_data.h"
#include "audio/game/game_sound_driver.h"
#include "engine/combat/attack.h"
#include "engine/combat/combat_script.h"
#include "engine/combat/movement.h"
#include "engine/combat/session_combat_view.h"
#include "engine/config/pc_config.h"
#include "engine/entities/entity_script.h"
#include "engine/game_state/game_ids.h"
#include "engine/ids/game_numbers.h"
#include "engine/items/powered_weapon.h"
#include "engine/loop/frame_view.h"
#include "engine/loop/game_random.h"
#include "engine/player/player.h"
#include "engine/screens/screens_view.h"
#include "engine/script/sh_script.h"
#include "engine/world/game/character_world.h"
#include "input/object_pick.h"
#include "render/decals/pc_decals.h"

extern int g_DebugNoTarget;
extern int g_DebugThirdPersonCam;
extern std::int32_t g_PcBulletHitActive;

extern "C" {
void Vibration_Play(std::int32_t effect);
void Vibration_MotorWeaponRumble(std::int32_t chainsaw);
void Savegame_EnemyStateUpdate(void* chara);
// Positional sounds (src/audio/game/placed_sound.cpp).
void Sfx_WithFlagsPlay(int sfx, const void* position, std::int32_t volume, std::int32_t flags);
void Sfx_WithFlagsAndPitchPlay(int sfx, const void* position, std::int32_t volume, std::int32_t flags,
                               std::int32_t pitch);
void Sfx_WithFalloffAndPitchPlay(int sfx, void* position, std::int32_t volume, std::int32_t falloff,
                                 std::int8_t pitch);
// Blood and the clip colour over records (src/engine/effects/game/combat_effects_host.cpp).
void Chara_EffectSpawn(void* chara, const void* position, std::int32_t strength, std::int32_t kind);
void Chara_AnimStatusSet(std::int32_t handover, void* chara, std::int32_t status, std::int32_t keyframe);
// The active clip of a model (src/engine/entities/game/model_runtime.cpp).
void* ModelAnim_ActiveInfoGet(void* anim);
}

namespace cb = sh::combat;
namespace cg = sh::chara::game;
namespace ent = sh::entities;
namespace wg = sh::world::game;

namespace {

// The packed status of the player's first map clip.
constexpr std::uint8_t kPlayerMapClipStart = 38 << 1;
// The update flag of a positional sound that keeps playing.
constexpr std::int32_t kSoundKeep = 4;

ent::Vec3 vec(const cb::Vec3& v) { return ent::Vec3{v.x, v.y, v.z}; }
cb::Vec3 vec(const ent::Vec3& v) { return cb::Vec3{v.x, v.y, v.z}; }

int sound_id(std::string_view name) {
    static std::unordered_map<std::string, int> ids;
    const auto it = ids.find(std::string(name));
    if (it != ids.end()) {
        return it->second;
    }
    const int id = sh::ids::sound_number(std::string(name).c_str());
    ids.emplace(std::string(name), id);
    return id;
}

// The gas weapon channels' sound names.
const char* gas_sound_name(cb::GasSound channel) {
    switch (channel) {
        case cb::GasSound::Contact: return "gas_weapon_contact";
        case cb::GasSound::ChainsawRun: return "chainsaw_attack";
        case cb::GasSound::RockDrillStart: return "rock_drill_start";
        case cb::GasSound::RockDrillRun: return "rock_drill_attack";
    }
    return "";
}

// Types whose data says "single_clip_table": any table index attaches their first table.
bool single_table_type(int chara_id) {
    const sh::chara::CharacterDef* def = sh::chara::type_def(chara_id);
    return def != nullptr && def->single_clip_table;
}

// The bones the NPC loop hands the type's update (the type's shared set) and their count; nullptr when the type has
// no animation loaded (the player always has his).
ent::BoneCoord* bones_of(int slot, int* count) {
    const cg::link::Skeleton sk = cg::link::skeleton(slot);
    *count = sk.animation != nullptr ? sk.bone_count : 0;
    if (slot != 0 && sk.animation == nullptr) {
        return nullptr;
    }
    return sk.bones;
}

class GameCombatView final : public cb::SessionCombatView, public ent::EnemyHost {
public:
    // CombatView + MoveView
    std::int32_t delta_time() const override { return sh::loop::frame_delta(); }

    // SessionCombatView
    bool no_collide(int slot) const override { return sh::input::pick_no_collide(record(slot)) != 0; }
    cb::CombatTraits traits(int chara_id) const override {
        cb::CombatTraits t;
        const sh::chara::CharacterDef* def = sh::chara::type_def(chara_id);
        if (def == nullptr) {
            return t;
        }
        t.blood_kind = def->blood_kind;
        t.blood_at_zero_health = def->blood_at_zero_health;
        t.blood_strength = def->blood_strength;
        t.fresh_hit_blood_strength = def->fresh_hit_blood_strength;
        t.fresh_hit_blood_strength_scatter = def->fresh_hit_blood_strength_scatter;
        t.impact_sound = def->impact_sound;
        t.is_player = def->is_player;
        t.enemy = def->enemy;
        return t;
    }

    // CombatView
    bool room_fading_in() const override { return cg::link::room_fading_in(); }
    bool no_target_cheat() const override { return g_DebugNoTarget != 0; }
    bool alternate_camera() const override { return g_DebugThirdPersonCam != 0; }
    bool bullet_fat_hitbox() const override { return g_PcConfig.bulletFatHitbox != 0; }
    bool melee_once_per_swing() const override { return g_PcConfig.meleeOncePerSwing != 0; }
    std::int32_t aim_time() const override { return sh::game::player().attack_time(); }
    int hyper_blaster_color() const override {
        return sh::items::hyper_blaster_beam_color(static_cast<std::uint8_t>(cg::link::special_items_picked_up()));
    }
    std::int32_t random_q12() override { return sh::loop::game_random().next_q12_fraction(); }

    sh::items::WeaponAttack attack_row(int attack) const override {
        if (attack < 0 || attack >= cg::link::attack_count()) {
            return sh::items::WeaponAttack{};
        }
        return cg::link::attack_row(attack);
    }
    int attack_count() const override { return cg::link::attack_count(); }
    int attack_number(std::string_view name) const override { return cg::link::attack_number(name); }

    std::int32_t swing_start_keyframe(int slot) const override {
        // The animation an attack is timed by: Harry's upper body, an NPC's whole body.
        ent::ModelAnimRecord* anim = slot == cb::kPlayerSlot ? &cg::slots().player_upper.model.anim
                                     : chara(slot) != nullptr ? &chara(slot)->model.anim
                                                              : nullptr;
        return anim != nullptr ? static_cast<cg::ClipInfo*>(ModelAnim_ActiveInfoGet(anim))->start_keyframe : 0;
    }

    void bind_player_clips() override {
        const void* base = nullptr;
        const void* map = nullptr;
        cg::link::player_clip_tables(&base, &map);
        for (ent::ModelAnimRecord* anim : {&cg::slots().player.model.anim, &cg::slots().player_upper.model.anim}) {
            anim->clip_table = base;
            anim->map_status_start = kPlayerMapClipStart;
            anim->map_clip_table = map;
        }
    }

    cb::ShotTrace trace_shot(const cb::Vec3& from, const cb::Vec3& offset, int exclude_slot, bool fat) override {
        // The game kept the trace in one static record between shots; so does this.
        static cg::RayRecord trace;
        const ent::Vec3 start = vec(from);
        ent::Vec3 ray = vec(offset);
        g_PcBulletHitActive = fat ? 1 : 0;
        const bool hit = wg::trace(cg::world_record(trace),
                                   cg::world_vec(start), cg::world_vec(ray), wg::TraceKind::Chara,
                                   chara(exclude_slot));
        g_PcBulletHitActive = 0;
        cb::ShotTrace out;
        out.hit = hit;
        out.point = vec(trace.target);
        out.character = trace.character != nullptr;
        out.target_slot = trace.character != nullptr ? slot_of(trace.character) : -1;
        return out;
    }
    bool line_blocked(const cb::Vec3& from, const cb::Vec3& to) override {
        static cg::RayRecord trace;
        // The offset wraps like the game's 32-bit coordinates.
        const auto wrap_sub = [](std::int32_t a, std::int32_t b) {
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
        };
        const sh::world::Vec3 offset{wrap_sub(to.x, from.x), wrap_sub(to.y, from.y), wrap_sub(to.z, from.z)};
        return wg::trace(cg::world_record(trace), sh::world::Vec3{from.x, from.y, from.z}, offset, wg::TraceKind::Solid,
                         nullptr);
    }

    void rumble(int pattern) override { Vibration_Play(pattern); }
    void motor_rumble(bool chainsaw) override { Vibration_MotorWeaponRumble(chainsaw ? 1 : 0); }
    void play_sound(std::string_view name, const cb::Vec3& position, std::int32_t volume_q8) override {
        const ent::Vec3 p = vec(position);
        Sfx_WithFlagsPlay(sound_id(name), &p, volume_q8, 0);
    }
    void blood(int slot, const cb::Vec3& position, int strength, int kind) override {
        const std::int32_t p[4] = {position.x, position.y, position.z, 0};
        Chara_EffectSpawn(chara(slot), p, strength, kind);
    }
    void bullet_impact(const cb::Vec3& point, const cb::Vec3& direction, const cb::Vec3& origin) override {
        sh::render::bullet_decal_add({point.x, point.y, point.z}, {direction.x, direction.y, direction.z},
                                     {origin.x, origin.y, origin.z});
    }
    void count_attack(cb::AttackStat stat, bool hit, std::int32_t aim_time) override {
        sh::screens::count_player_attack(static_cast<std::int8_t>(stat), hit, aim_time);
    }

    void gas_sound_start(cb::GasSound channel, std::int32_t volume, std::int32_t pitch) override {
        Sfx_WithFlagsPlay(sound_id(gas_sound_name(channel)), &cg::slots().player.position, volume, pitch);
    }
    void gas_sound_keep(cb::GasSound channel, std::int32_t volume, std::int32_t pitch) override {
        Sfx_WithFlagsAndPitchPlay(sound_id(gas_sound_name(channel)), &cg::slots().player.position, volume, kSoundKeep,
                                  pitch);
    }
    void gas_sound_stop(cb::GasSound channel) override {
        sh::audio::game_sound_driver().stop_effect(static_cast<std::uint16_t>(sound_id(gas_sound_name(channel))));
    }

    // MoveView
    std::int32_t collide(int slot, const cb::Vec3& offset, cb::MoveSurface& surface) override {
        cg::WallResult result = {};
        const ent::Vec3 step = vec(offset);
        const std::int32_t code = wg::move_character(cg::world_record(result), cg::world_vec(step), *chara(slot));
        surface.offset = vec(result.offset);
        surface.ground_height = result.surface.height;
        surface.ground_type = result.surface.kind;
        return code;
    }

    // EnemyHost
    bool player_visible(int slot, std::int32_t mode, std::int32_t distance, std::int32_t angle) override {
        return wg::sees_player(*chara(slot), mode, distance, angle);
    }
    std::int32_t player_noise(int slot, std::int32_t distance, std::int32_t angle) override {
        return wg::player_loudness(*chara(slot), distance, static_cast<std::int16_t>(angle));
    }
    bool player_blocked(int slot) override { return wg::wall_toward_character(*chara(slot), cg::slots().player); }
    bool path_blocked(int slot, std::int32_t distance, std::int32_t angle) override {
        return wg::wall_ahead(*chara(slot), distance, static_cast<std::int16_t>(angle));
    }
    bool path_hit(int slot, std::int32_t distance, std::int32_t angle) override {
        return wg::blocked_ahead(*chara(slot), distance, static_cast<std::int16_t>(angle));
    }
    std::int32_t clear_heading(int slot, std::int32_t distance, std::int32_t angle) override {
        return wg::clear_heading(*chara(slot), distance, static_cast<std::int16_t>(angle));
    }
    std::int32_t heading_toward(int slot, std::int32_t distance, std::int32_t x, std::int32_t z, std::int32_t span,
                                bool nearest) override {
        return wg::heading_toward(*chara(slot), distance, x, z, static_cast<std::int16_t>(span), nearest);
    }
    void enemy_defeated(int slot) override { Savegame_EnemyStateUpdate(chara(slot)); }
    void anim_status_set(int slot, std::int32_t status, std::int32_t keyframe) override {
        Chara_AnimStatusSet(0, chara(slot), status, keyframe);
    }
    void sfx_3d(int slot, int sfx, std::int32_t volume_q8, std::int32_t kind) override {
        Sfx_WithFlagsPlay(sfx, &chara(slot)->position, volume_q8, kind);
    }
    void sfx_falloff(int slot, int sfx, std::int32_t volume, std::int32_t falloff_q12, std::int32_t pitch) override {
        Sfx_WithFalloffAndPitchPlay(sfx, &chara(slot)->position, volume, falloff_q12, static_cast<std::int8_t>(pitch));
    }
    bool clip_duration(int slot, int status, std::int32_t* speed) override {
        const ent::CharacterRecord& r = *chara(slot);
        const cg::ClipTableRef t = cg::active_clip_table(r.model.chara_id, r.model.anim.clip_table);
        if (t.clips == nullptr) {
            return false;
        }
        *speed = t.clips[status].speed;
        return true;
    }
    bool clip_bound_set(int slot, int status, bool end, std::int32_t keyframe) override {
        const ent::CharacterRecord& r = *chara(slot);
        const cg::ClipTableRef t = cg::active_clip_table(r.model.chara_id, r.model.anim.clip_table);
        if (t.clips == nullptr) {
            return false;
        }
        if (end) {
            t.clips[status].end_keyframe = static_cast<std::int16_t>(keyframe);
        } else {
            t.clips[status].start_keyframe = static_cast<std::int16_t>(keyframe);
        }
        return true;
    }
    bool clip_table_attach(int slot, int index) override {
        ent::CharacterRecord* r = chara(slot);
        cg::ClipTableRef t;
        if (!cg::type_clip_table(r->model.chara_id, single_table_type(r->model.chara_id) ? 0 : index, &t)) {
            return false;
        }
        r->model.anim.clip_table = t.clips;
        r->model.anim.map_clip_table = nullptr;
        return true;
    }
    int bone_count(int slot) override {
        int count = 0;
        return bones_of(slot, &count) != nullptr ? count : -1;
    }
    void root_transform(int slot) override {
        // The root bone follows the record's position and rotation.
        int count = 0;
        ent::CharacterRecord* r = chara(slot);
        cg::bones::place_root(*bones_of(slot, &count), r->position, r->rotation);
    }
    void bone_rotate(int slot, int bone, std::int16_t rx, std::int16_t ry, std::int16_t rz) override {
        int count = 0;
        ent::BoneCoord* bones = bones_of(slot, &count);
        cg::bones::turn_own(bones[bone], sh::math::rot_matrix_zxy_neg({rx, ry, rz}));
    }
    std::array<std::int32_t, 3> bone_point(int slot, int bone, std::int16_t x, std::int16_t y,
                                           std::int16_t z) override {
        int count = 0;
        ent::BoneCoord* bones = bones_of(slot, &count);
        const cg::bones::World m = cg::bones::world(bones[bone]);
        const long long v[3] = {x, y, z};
        std::array<std::int32_t, 3> out{};
        for (int row = 0; row < 3; ++row) {
            const long long sum = static_cast<long long>(m.t[row]) * 4096 + m.rotation.m[row][0] * v[0] +
                                  m.rotation.m[row][1] * v[1] + m.rotation.m[row][2] * v[2];
            out[static_cast<std::size_t>(row)] = static_cast<std::int32_t>(sum >> 12);
        }
        return out;
    }

private:
    ent::CharacterRecord* chara(int slot) const { return record(slot); }
};

GameCombatView& view() {
    static GameCombatView v;
    return v;
}

}  // namespace

extern "C" {

int PcCombat_AttackUpdate(_SubCharacter* chara) { return cb::update_attack(view(), view().slot_of(chara)); }

void PcCombat_AttackSettle(_SubCharacter* chara) { cb::settle_attack(view(), view().slot_of(chara)); }

void PcCombat_AttackTickReset(_SubCharacter* chara) { view().set_attack_ticked(view().slot_of(chara), false); }

int PcCombat_Move(_SubCharacter* chara) { return cb::move_character(view(), view().slot_of(chara), 0, 0); }

int PcCombat_NpcIndexOf(const _SubCharacter* chara) {
    const int slot = view().slot_of(chara);
    if (slot == cb::kPlayerSlot) {
        return ent::kNpcSlotCount;
    }
    return slot > 0 ? slot - 1 : -1;
}

void PcCombat_GasSoundStopAll(void) { cb::gas_sound_stop_all(view()); }
void PcCombat_GasSoundContact(int volume) { cb::gas_sound_contact(view(), volume); }
void PcCombat_GasSoundMotor(int chainsaw, int volume, int pitch) {
    cb::gas_sound_motor(view(), chainsaw != 0, volume, pitch);
}
void PcCombat_GasSoundDrillRun(int volume, int pitch) { cb::gas_sound_drill_run(view(), volume, pitch); }

void PcCombat_BindScript(ShScriptGameApi* api) {
    // The enemy group of sh.entity.* first, then the combat module's calls in front of it.
    ent::bind_entity_enemy_script(*api, view());
    cb::bind_combat_script(*api, view(), view());
}

}  // extern "C"

// Registration order: sh_script.h.
static ShScriptGameBinderEntry s_ScriptBinder(90, PcCombat_BindScript);
