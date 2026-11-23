// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Accessors the named script binds (src/engine/scripting/binds) need over the running game's records; implemented
// over engine state by record_links.cpp and src/engine/scripting/script_game_link.cpp.

#pragma once

#include <cstdint>

#include "engine/effects/slot_effects.h"
#include "engine/math/view_projection.h"

namespace sh::script::links {

// Character slots: 0 the player, 1.. the NPCs.
bool chara_slot_valid(int slot);
// The active clip's duration (Q12 keyframes per second) and first keyframe; false when the character has no clips.
bool chara_active_clip(int slot, std::int32_t* duration, std::int32_t* start_keyframe);
// One update of the NPC's Lua behaviour now.
void chara_behavior_update(int slot);
// One step along the event waypoints of slot 1 without advancing the event step (when the map has the walker).
void chara_path_walk_no_wait(int slot, int anim, int mode);
// The carousel words of the NPC block: legs clip state, horse ridden, the horse's height (Q12).
std::int32_t chara_ride_state(int slot);
std::int32_t chara_ride_horse(int slot);
void chara_set_ride_height(int slot, std::int32_t height);
std::int32_t chara_split_head_flags(int slot);
// The address of the slot's position (the effects keep it and read it every frame).
void* chara_position_record(int slot);
// The slot's record cleared to an idle character of `type`.
void chara_record_reset(int slot, int type);

// Character types of the loaded map.
// Character types 1..kCharaTypes - 1 (0 is none).
constexpr int kCharaTypes = 256;
// ScriptedAs: the type runs type `as`'s behaviour (a boss playing another type's fight).
enum class CharaUpdate { Scripted, ScriptedAlt, ScriptedAs };
void chara_update_set(int type, CharaUpdate update, int as = -1);
// The character whose animations the animation slot of chara group `group` holds.
int chara_group_anim_chara(int group);
void chara_anim_file_set(int type, int file);
// The NPC bone buffer from index `index` (nullptr past its end).
void* npc_bones(int index);
void chara_anim_data_alloc(int anim_slot, int type, void* buffer, void* bones);
void chara_model_load(int type, int model_slot, void* buffer, int page, int u, int v, int palette_x, int palette_y);

// File buffers by number (0..2, 26, 29..34) and the second model buffer; nullptr for any other.
void* file_buffer(int number);
void* model_buffer_2();
// Queues a TIM read (file number) through `buffer` onto the place given; returns the queue entry.
int queue_tim_at(int file, void* buffer, int page, int u, int v, int palette_x, int palette_y);
// Queues a TIM read (file number) as the loading screen's background image; returns the queue entry.
int queue_loading_screen_image(int file);

// Harry's map clips: `count` clips of the animation table `table` from `first` on. 0 done, 1 unknown table,
// 2 bad range.
int player_map_anim_infos_set(int first, const char* table, int count);
// Harry's map keyframe ranges: `count` rows of the loaded map's array `symbol` from `first` on. 0 done, 1 no
// such array, 2 bad range.
int player_map_anim_ranges_set(int first, const char* symbol, int count);

// One more draw this tick of the loaded map's world object `name` at that pose; false with err set.
bool world_object_draw(const char* name, const std::int32_t position[3], const std::int16_t rotation[3], char* err,
                       int err_cap);

// The world matrix of a bone (rotation Q12 rows, translation), brought up to date first.
void bone_world_matrix(void* bone, std::int16_t rotation[3][3], std::int32_t translation[3]);

// The map effect slot table: whether the map has one, and the smoke column's parameter block.
bool effect_slots_present();
bool smoke_column_present();
int effect_slot_count();
sh::effects::EffectSlot effect_slot_load(int slot);
void effect_slot_store(int slot, const sh::effects::EffectSlot& s);
// The table's allocator: the slot marked with `kind`, -1 when the table is full.
int effect_slot_alloc(int kind);

// The game's view at a world point (Q12), as the projection the panels go through: the screen and precise switches
// in force now, the exact camera of the precise path.
sh::math::ViewProjection view_at(std::int32_t x, std::int32_t y, std::int32_t z);
// Corners carry their view-space positions (the renderer's flashlight and clipping want them).
bool precise_view_space();

}  // namespace sh::script::links
