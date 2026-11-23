// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The narrow link of the character hosts (src/engine/chara/game: the behaviour API of the types, the combat and
// entity views, the asset pool, the map cast helpers) to the rest of the game:
// the skeletons the animation slots pose, the map's variables, the clip tables, the weapon attack table, the file
// queue and the model registry. The implementation is the engine's: chara_link_game.cpp (slots, models, attack
// table, the map's variables), chara_link_map.cpp and clip_tables.cpp. Engine types only.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "engine/chara/game/clip_info.h"
#include "engine/entities/character_record.h"
#include "engine/items/item_library.h"

namespace sh::chara::game::link {

// --- Skeletons ---

// The bones the NPC loop hands a slot's type (NPCs of one type share one set) and the animation file they pose
// from; empty when the type has none loaded. Slot 0 is the player.
struct Skeleton {
    sh::entities::BoneCoord* bones = nullptr;
    void* animation = nullptr;  // the animation header
    int bone_count = 0;
};
Skeleton skeleton(int slot);
// The bones a clip of `animation` writes (one bit per bone).
void set_active_bones(void* animation, std::uint32_t mask);
// Where the game keeps Harry's base animation file and his map's file.
void harry_anim_buffers(const void** base, const void** map);

// --- Clip tables ---

// A table of gamedata/tables/anim/<name>.json as the game holds it (refilled with every map load); empty when none.
ClipTableRef anim_table(const char* name);
// The playback functions of the clip kinds (blend into, once, loop).
ClipPlayback clip_playback_blend();
ClipPlayback clip_playback_once();
ClipPlayback clip_playback_loop();
// The player's base clips and the map's clips of the player.
void player_clip_tables(const void** base, const void** map);

// --- The map overlay ---

// Variables of the overlay a behaviour shares with the overlay's own code; nullptr when the map has none.
enum class OverlayVariable : std::uint8_t {
    DahliaEffectSpawned,   // u8: Dahlia's control state 19 spawned its effect
    WallResult,            // the collision result block the overlay reads back after a wall test
    AirScreamerFlyby,      // s32: map0_s01's fly-by moves the bird
    StalkerSamples,        // u8
    StalkerPrevX,          // s32
    StalkerPrevZ,          // s32
    StalkerSpeed,          // s32[5]
    StalkerHeading,        // u16[5]
    StalkerPredictX,       // s32
    StalkerPredictZ,       // s32
};
// The overlay's copy, else the executable's own copy where it keeps one (the stalker track), else nullptr.
void* overlay_variable(OverlayVariable v);

// The update the map header holds for a type (nullptr: none).
CharaUpdate map_chara_update(int chara_id);
void set_map_chara_update(int chara_id, CharaUpdate update);
// The boss maps' blood drawers (Split Head); false when the map has none.
bool map_blood_drop(std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t speed, std::int32_t heading);
bool map_blood_strand(std::int32_t kind, std::int32_t alpha, std::int32_t x0, std::int32_t y0, std::int32_t z0,
                      std::int32_t x1, std::int32_t y1, std::int32_t z1);
// The map draws acid drops (its header has the drawer).
bool map_has_acid_drawer();
// Where an attacker that pins Harry down at (x, z) facing `angle` ends up, and Harry pushed by (x, z) with a knock
// vector (the map's player code).
void player_pin_point(std::int32_t& x, std::int32_t& z, std::int16_t& angle);
// `pusher`: the pushing slot, 0 for the boss map's own push (an engine spawn off the boss maps passes its slot).
void player_push(std::int32_t x, std::int32_t z, const sh::entities::Vec3& knock, int pusher = 0);
// The lead the boss effects computed (Q12 per second, x and z).
void boss_aim_lead(std::int32_t& x, std::int32_t& z);

// --- Types with their own records in the map ---

// The Puppet Nurse stats entry a record points at (0..3), -1 when none; points the record at entry `index`.
int puppet_nurse_stats(int slot);
void set_puppet_nurse_stats(int slot, int index);
// The Puppet Nurse clip tables: 0 the nurses', 1 the doctor's.
ClipTableRef puppet_nurse_clips(int index);

// --- Models ---

// The type's model has its bone nodes loaded; every node drawn / node `node` collapsed this frame (nothing without
// the model).
bool model_bones_present(int chara_id);
void model_bones_show(int chara_id);
void model_bone_hide(int chara_id, int node);
// The effect texture of the Twinfeeler's dust clouds starts loading.
void twinfeeler_dust_texture_load();

// --- The model and animation registries (the global asset pool) ---

// The disc files of a type's model, texture and animations (the file table's numbers, -1 none).
struct CharaFiles {
    std::int16_t model = -1;
    std::int16_t texture = -1;
    std::int16_t anim = -1;
};
CharaFiles chara_files(int chara_id);
void set_chara_files(int chara_id, const CharaFiles& files);
// The model record: its size, and its fill for a load of `chara_id` into `lm` with the texture at `texture`
// (the image place of the file queue, 8 bytes).
std::size_t chara_model_bytes();
void chara_model_reset(void* model, int chara_id, void* lm, const void* texture);
void chara_model_set_ticket(void* model, std::int32_t ticket);
bool chara_model_loaded(const void* model);
// The model's file finished loading: its parts and texture are registered.
void chara_model_process_load(void* model);
// The model the draw uses for a type, and the type a model record holds.
void* registered_model(int chara_id);
void set_registered_model(int chara_id, void* model);
int model_chara(const void* model);
// The animation slots: the slot a type poses from (-1 none), the slot count of the map's groups, the slot the
// pool keeps a type in, what a slot holds, and loading a type's animation file into a slot.
int anim_index(int chara_id);
void set_anim_index(int chara_id, int index);
int anim_group_count();
int pool_anim_index(int chara_id);
struct AnimSlot {
    int chara = 0;
    const void* header = nullptr;
};
AnimSlot anim_slot(int index);
void anim_slot_clear(int index);
void anim_alloc(int index, int chara_id, void* buffer, sh::entities::BoneCoord* bones);
// The scratch buffer a texture file is read into before its upload.
void* image_read_buffer();

// --- Weapons ---

int attack_count();
sh::items::WeaponAttack attack_row(int attack);
// The index of the attack row named `name` (attacks.json "name"), -1 when none.
int attack_number(std::string_view name);
// The picked up special items (the Hyper Blaster's colour).
int special_items_picked_up();

// --- Screen ---

// The reveal after a load is running (no hit lands).
bool room_fading_in();

}  // namespace sh::chara::game::link
