// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// What the engine's world objects, runtime pickups and mod hook events (src/engine/runtime) ask of the game that
// game_state.h does not cover: the draw records the world drawer reads, the game's names of things by number
// and back, the item prompt, and a few collision queries.
//
// Implemented over the engine in game_state_world.cpp (draw records: engine/runtime/world_object_record.h).

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sh::game_state {

// ---- draw records of world objects ----
// The world drawer keeps pointers to the records it was handed until the frame is drawn, and caches the model it
// found in the record, so a record lives as long as its object. A map variable bound to world objects by name
// ("wo:<name>") addresses the records directly: those of one map are one contiguous array.

struct ObjectPose {
    std::array<std::int32_t, 3> position{};  // Q12 metres
    std::array<std::int16_t, 3> rotation{};  // Q12 turns
    char model[9] = {};
};

// The map's array: at least `count` records; [0, count) are cleared (no model, zero pose).
void world_object_records_reset(std::size_t count);
// The record's model by name (1..8 characters); the cached model lookup starts over.
void world_object_record_model(std::size_t index, const char* name);
void world_object_record_set_pose(std::size_t index, const std::array<std::int32_t, 3>& position,
                                  const std::array<std::int16_t, 3>& rotation);
ObjectPose world_object_record_pose(std::size_t index);
// Hands the record to this frame's world draw list.
void world_object_record_draw(std::size_t index);
// The library the world drawer found the record's model in (ObjectModel::library; 0: none yet).
std::int32_t world_object_record_library(std::size_t index);
// The record's address for map variables bound by name.
void* world_object_record_address(std::size_t index);

// Records of runtime pickups: one each, created with the model, kept until pickup_records_clear (a removed pickup's
// record may still be in this frame's draw list).
using PickupRecord = void*;
PickupRecord pickup_record_create(const char* model);
void pickup_record_set_pose(PickupRecord record, const std::array<std::int32_t, 3>& position,
                            const std::array<std::int16_t, 3>& rotation);
void pickup_record_draw(PickupRecord record);
void pickup_records_clear();

// The engine's "near" test of pc_near_pickup world objects (interiors: the neighbouring cells on the same storey).
bool common_pickup_in_range(const std::array<std::int32_t, 3>& position);

// ---- names of the game's things ----
int item_number(const char* name);            // <= 0 unknown
int flag_number(const char* name);            // < 0 unknown
int common_pickup_number(const char* name);   // -1: the item has no common pickup kind
const char* item_name(int item);              // full id, nullptr when none
const char* flag_name(int flag);              // full id, nullptr when none
const char* chara_name(int chara);            // full id, nullptr when none
const char* item_world_model(const char* name);  // the item's world model, nullptr when the item has none

// ---- the item prompt of a pickup ----
// Starts the synthetic button event a runtime pickup runs under (the map event a pickup of the map data has).
void start_pickup_event();
// That synthetic event is the current map event.
bool pickup_event_current();
// Starts the synthetic button event a script's own event runs under (sh.event.run, engine/world/script_event.h).
void start_script_event();
// That synthetic event is the current map event.
bool script_event_current();
// Starts a synthetic button event that shows message `message` of the map (a mod's sh.world.message), like a
// message event of the map data: the world stops until the message is read.
void start_message_event(int message);
// The game's item prompt, one step per call (the map scripts' item take and common item take).
void item_take(int item, int count, int flag, int message);
void common_item_take(int kind, int flag);
int common_pickup_message(int kind);
// The map point of the current map event, false when there is none (or no point table).
bool current_map_event_point(std::int32_t* x, std::int32_t* z);

// ---- collision queries of mods ----
bool cell_has_collision(std::int32_t x, std::int32_t z);
// A ray from `from` to `to` (Q12, +Y down) hits geometry.
bool ray_blocked(const std::array<std::int32_t, 3>& from, const std::array<std::int32_t, 3>& to);
inline constexpr int kGroundTypeNone = 12;  // ground_sample().type where there is no ground

// ---- characters spawned by mods ----
int chara_find(const char* name);          // <= 0 unknown
int spawn_ai_find(const char* name);       // < 0 unknown
int spawn_character_ai(int chara, int state, int x, int z, int yaw, int ai);  // slot, < 0 failed
const char* chara_spawn_note(int chara);   // why it cannot be spawned, "" when nothing is known

}  // namespace sh::game_state
