// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The calls of the character link (chara_link.h) over the running game's engine records: the animation slots
// (engine/gameflow/game/anim_slots_host.cpp), the session's bones, the model registry (CharaModels), the map's
// variables found by name, the attack table and the running save. Also the map cast's entry for the map loader and
// the words of Harry's map scenes the player host asks for.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/loader/active_map.h"
#include "engine/app/loader/game_files.h"
#include "engine/chara/game/chara_link.h"
#include "engine/chara/game/chara_records.h"
#include "engine/chara/game/map_cast.h"
#include "engine/chara/runtime/chara_models.h"
#include "engine/entities/character_record.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/anim_slots.h"
#include "engine/inventory/game/attack_table.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/memory/work_buffers.h"
#include "engine/maps/map_runtime.h"
#include "engine/player/player_records.h"
#include "engine/screens/screens_view.h"
#include "engine/app/loader/picture_places.h"

namespace cg = sh::chara::game;
namespace ent = sh::entities;

extern "C" {
// The map's own variables by name (engine/loader/active_map.h).
// The boss's drool and Harry's drag mark (engine/effects/game/map_effect_frame_host.cpp).
void SlotFx_DroolSpawn(std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t speed, std::int32_t heading);
void SlotFx_DragSpawn(std::int32_t row, std::int32_t brightness, std::int32_t x0, std::int32_t y0, std::int32_t z0,
                      std::int32_t x1, std::int32_t y1, std::int32_t z1);
// Harry's map code: an attacker pinning him down and a shove (engine/entities/game/player_scene_host.cpp).
void Player_PinUpdate(std::int32_t* x, std::int32_t* z, const std::int16_t* angle);
void Player_PushUpdate(std::int32_t x, std::int32_t z, const std::int32_t* push, int pusher);
// The lead the final boss's effects computed (engine/effects/game/ending_effects_host.cpp).
void BossFx_AimLead(std::int32_t* x, std::int32_t* z);
}

namespace sh::chara::game::link {
namespace {

constexpr int kGroupSlots = 4;    // the player's slot and the map's three character groups
constexpr int kAnmBoneCount = 6;    // the ANM header's bone count byte
constexpr int kAnmActiveBones = 8;  // the ANM header's word of bones a clip writes


// The fade's states: 6 and up is the reveal after a load.
constexpr std::int32_t kFadeInStart = 6;

sh::gameflow::AnimSlotTable& anim_slots() { return sh::gameflow::anim_slot_table(); }

int slot_of_type(int chara_id) {
    if (chara_id <= 0) {
        return -1;
    }
    const int idx = anim_slots().chara_slot_of(chara_id);
    return anim_slots().has_slot(idx) ? idx : -1;
}

Skeleton skeleton_of(void* bones, void* anm) {
    const int count = anm != nullptr ? static_cast<const std::uint8_t*>(anm)[kAnmBoneCount] : 0;
    return {static_cast<ent::BoneCoord*>(bones), anm, count};
}

// The Stalker's tracking words where the map's tables have none: one set for the whole run.
struct StalkerTrack {
    std::uint8_t samples = 0;
    std::int32_t prev_x = 0;
    std::int32_t prev_z = 0;
    std::int32_t speeds[8] = {};
    std::uint16_t headings[6] = {};
    std::int32_t predict_x = 0;
    std::int32_t predict_z = 0;
} s_stalker;

}  // namespace

Skeleton skeleton(int slot) {
    if (slot == 0) {
        return skeleton_of(sh::game::session().system.characters.player_bones, anim_slots().slots[0].active_header);
    }
    const ent::CharacterRecord* r = slot > 0 ? cg::record(slot) : nullptr;
    const int idx = r != nullptr ? slot_of_type(r->model.chara_id) : -1;
    if (idx < 0) {
        return {};
    }
    return skeleton_of(anim_slots().slots[idx].bone_coords, anim_slots().slots[idx].active_header);
}

void set_active_bones(void* animation, std::uint32_t mask) {
    std::memcpy(static_cast<std::uint8_t*>(animation) + kAnmActiveBones, &mask, sizeof(mask));
}

void harry_anim_buffers(const void** base, const void** map) {
    *base = sh::memory::work_buffers().data(sh::memory::WorkBuffer::PlayerBaseAnim),
    *map = sh::memory::work_buffers().data(sh::memory::WorkBuffer::PlayerMapAnim);
}

void* overlay_variable(OverlayVariable v) {
    const auto symbol = [](const char* name, void* own) {
        void* p = sh::loader::active_map_symbol(name);
        return p != nullptr ? p : own;
    };
    switch (v) {
        case OverlayVariable::DahliaEffectSpawned: return sh::loader::active_map_symbol("dahlia_effect_spawned");
        case OverlayVariable::WallResult: return sh::loader::active_map_symbol("wall_collision_result");
        case OverlayVariable::AirScreamerFlyby: return sh::loader::active_map_symbol("g_PcAsFlybyActive");
        case OverlayVariable::StalkerSamples: return symbol("stalker_samples", &s_stalker.samples);
        case OverlayVariable::StalkerPrevX: return symbol("stalker_prev_x", &s_stalker.prev_x);
        case OverlayVariable::StalkerPrevZ: return symbol("stalker_prev_z", &s_stalker.prev_z);
        case OverlayVariable::StalkerSpeed: return symbol("stalker_speeds", s_stalker.speeds);
        case OverlayVariable::StalkerHeading: return symbol("stalker_headings", s_stalker.headings);
        case OverlayVariable::StalkerPredictX: return symbol("stalker_predict_x", &s_stalker.predict_x);
        case OverlayVariable::StalkerPredictZ: return symbol("stalker_predict_z", &s_stalker.predict_z);
    }
    return nullptr;
}

bool map_blood_drop(std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t speed, std::int32_t heading) {
    const bool drawn = sh::maps::map_runtime().draws(sh::maps::MapEffect::SplitHead);
    if (drawn) {
        SlotFx_DroolSpawn(x, y, z, speed, heading);
    }
    return drawn;
}

bool map_blood_strand(std::int32_t kind, std::int32_t alpha, std::int32_t x0, std::int32_t y0, std::int32_t z0,
                      std::int32_t x1, std::int32_t y1, std::int32_t z1) {
    const bool drawn = sh::maps::map_runtime().draws(sh::maps::MapEffect::SplitHead);
    if (drawn) {
        SlotFx_DragSpawn(kind, alpha, x0, y0, z0, x1, y1, z1);
    }
    return drawn;
}

void player_pin_point(std::int32_t& x, std::int32_t& z, std::int16_t& angle) { Player_PinUpdate(&x, &z, &angle); }

void player_push(std::int32_t x, std::int32_t z, const ent::Vec3& knock, int pusher) {
    const std::int32_t v[3] = {knock.x, knock.y, knock.z};
    Player_PushUpdate(x, z, v, pusher);
}

void boss_aim_lead(std::int32_t& x, std::int32_t& z) { BossFx_AimLead(&x, &z); }

bool model_bones_present(int chara_id) { return sh::chara::chara_models().registered(chara_id) != nullptr; }

void model_bones_show(int chara_id) {
    if (sh::world::SkeletonRecord* skel = sh::chara::chara_models().skeleton(chara_id)) {
        for (sh::world::SkeletonPart& part : skel->storage) {
            part.model.flags &= ~static_cast<std::int32_t>(sh::world::kPartCollapsed);
        }
    }
}

void model_bone_hide(int chara_id, int node) {
    if (sh::world::SkeletonRecord* skel = sh::chara::chara_models().skeleton(chara_id)) {
        skel->storage[node].model.flags |= static_cast<std::int32_t>(sh::world::kPartCollapsed);
    }
}

void twinfeeler_dust_texture_load() {
    sh::loader::queue_read_image(sh::loader::disc_files().find("TEST/WARMTEST.TIM"), image_read_buffer(), &sh::loader::picture_places().fire_effect);
}

std::size_t chara_model_bytes() { return sizeof(sh::chara::CharaModelRecord); }

void chara_model_reset(void* model, int chara_id, void* lm, const void* texture) {
    auto* m = static_cast<sh::chara::CharaModelRecord*>(model);
    std::memset(m, 0, sizeof(*m));
    m->chara = chara_id;
    m->loaded = 0;
    m->lm = static_cast<sh::world::LmHeader*>(lm);
    std::memcpy(&m->texture, texture, sizeof(m->texture));
}

void chara_model_set_ticket(void* model, std::int32_t ticket) {
    static_cast<sh::chara::CharaModelRecord*>(model)->ticket = ticket;
}

bool chara_model_loaded(const void* model) { return static_cast<const sh::chara::CharaModelRecord*>(model)->loaded != 0; }

void chara_model_process_load(void* model) {
    sh::chara::chara_models().process_load(*static_cast<sh::chara::CharaModelRecord*>(model));
}

void* registered_model(int chara_id) { return sh::chara::chara_models().registered(chara_id); }

void set_registered_model(int chara_id, void* model) {
    sh::chara::chara_models().set_registered(chara_id, static_cast<sh::chara::CharaModelRecord*>(model));
}

int model_chara(const void* model) { return static_cast<const sh::chara::CharaModelRecord*>(model)->chara; }

int anim_index(int chara_id) { return anim_slots().chara_slot_of(chara_id); }

void set_anim_index(int chara_id, int index) { anim_slots().set_chara_slot(chara_id, index); }

int anim_group_count() { return kGroupSlots; }

// The pool keeps every type in its own slot after the groups'.
int pool_anim_index(int chara_id) { return kGroupSlots + chara_id; }

AnimSlot anim_slot(int index) {
    if (!anim_slots().has_slot(index)) {
        return {0, nullptr};
    }
    return {anim_slots().slots[static_cast<std::size_t>(index)].active_chara,
            anim_slots().slots[static_cast<std::size_t>(index)].active_header};
}

void anim_slot_clear(int index) {
    if (anim_slots().has_slot(index)) {
        anim_slots().slots[static_cast<std::size_t>(index)] = sh::gameflow::AnimSlot{};
    }
}

void anim_alloc(int index, int chara_id, void* buffer, ent::BoneCoord* bones) {
    sh::gameflow::anim_slots().alloc(index, chara_id, buffer, bones);
}

void* image_read_buffer() { return sh::memory::work_buffers().data(sh::memory::WorkBuffer::FileStaging); }

int attack_count() { return sh::inventory::game::attack_row_count(); }

int attack_number(std::string_view name) { return sh::inventory::game::attack_number(name); }

sh::items::WeaponAttack attack_row(int attack) {
    const std::vector<sh::inventory::game::AttackRow>& table = sh::inventory::game::attack_table();
    if (attack < 0 || static_cast<std::size_t>(attack) >= table.size()) {
        return sh::items::WeaponAttack{};
    }
    const sh::inventory::game::AttackRow& r = table[static_cast<std::size_t>(attack)];
    // A weapon's row brings its combat rules (weapons/<Name>.json); the numbers below are the table's.
    const sh::items::WeaponAttack* weapon = sh::inventory::game::weapon_attack(attack);
    sh::items::WeaponAttack row = weapon != nullptr ? *weapon : sh::items::WeaponAttack{};
    row.index = attack;
    // The far reach and the knockback are unsigned Q4.12 in the game's rows; the near reach is signed.
    row.reach_q12 = static_cast<std::uint16_t>(r.reach);
    row.reach_start_q12 = r.reach_start;
    row.power = r.power;
    row.shape = {r.shape[0], r.shape[1], r.shape[2], r.shape[3], r.shape[4], r.shape[5]};
    row.knockback_q12 = static_cast<std::uint16_t>(r.knockback);
    row.keyframe_start = r.keyframe_start;
    row.keyframe_length = r.keyframe_length;
    row.hit_type = r.hit_type;
    row.hurt_sound = static_cast<std::int8_t>(r.hurt_sound);
    row.blood_splatter = r.blood_splatter;
    return row;
}

// Bits 3..7 of the save's mode byte.
int special_items_picked_up() { return sh::game::save_record().mode_bits >> 3; }

bool room_fading_in() { return (sh::screens::fade_status() & 0x7) >= kFadeInStart; }

}  // namespace sh::chara::game::link

// ---------------------------------------------------------------------------------------------
// The map loader asks for the map's cast; the words of Harry's map scenes (src/engine/entities/game).

namespace {

// The map's own word by name, else the run-long word of that name (map_globals.h).
void* overlay_word(const char* name) { return sh::loader::active_map_symbol(name); }

}  // namespace

extern "C" {

int Map_CharasSet(const char* mapName) { return cg::cast::load(mapName) ? 1 : 0; }

/* The weapon a scene put aside, for every map whose tables have no word of their own (all but map1_s04,
 * map2_s01, map2_s03, map2_s04): one word for the whole run, never reset by a map load, starting at 0xFF (none). */
std::uint8_t* HarryMap_WeaponStashWord(void) {
    static std::uint8_t shared = 0xFF;
    auto* word = static_cast<std::uint8_t*>(overlay_word("player_weapon_stash"));
    return word != nullptr ? word : &shared;
}

sh::game::WallResult* HarryMap_WallBlock(int own) {
    if (own != 0) {
        if (auto* block = static_cast<sh::game::WallResult*>(overlay_word("wall_collision_result"))) {
            return block;
        }
    }
    return &sh::game::session().player.floor;
}

/* The shove gathered so far (map1_s05's own word; other maps have none). */
std::int32_t* HarryMap_PushTotalWord(void) { return static_cast<std::int32_t*>(sh::loader::active_map_symbol("harry_push_total")); }

}  // extern "C"
