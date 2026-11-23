// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// More of the PC layer's view of the game (see game_state.h): what the PC HUD, the inventory mouse layer,
// the grab guard, the language layer and the model-file converters read and change in the game's records.
//
// Implemented on the game's own records by src/engine/game_state/game_state_frames.cpp.

#pragma once

#include "engine/game_state/game_state.h"

#include <cstddef>
#include <cstdint>

namespace sh::game_state {

// --- The loaded map's Harry reaction table ---
// Each map overlay carries a short table of Harry animations its own monsters need (grabs, pins), keyed by the
// packed animation status Harry's state machine asks for and mapped to an animation of the map's keyframe bank.
struct MapHarryAnimRow {
    int map_status = 0;    // the map bank's animation (packed status)
    int harry_status = 0;  // the state machine's status the row answers (packed status)
};
constexpr int kMapHarryAnimRowsMax = 40;  // the fixed count the game's own scan walks
// Row `idx` (0..kMapHarryAnimRowsMax-1) of the table; false when the map has no table or idx is out of range.
bool map_harry_anim_row(int idx, MapHarryAnimRow* out);

// --- Inventory screen ---
enum class InventorySelection : int {
    Item = 0,
    EquippedItem = 1,
    Exit = 2,
    Settings = 3,
    Map = 4,
    ItemCmd = 5,
    EquippedItemCmd = 6,
};
// The step of the inventory screen's top-level state (1 while the live inventory runs).
int inventory_screen_step();
int inventory_selection();
// Moves the selection to `id` with the region-change settle timer already run out, the bracket parked on it and
// the command cursor on row 0.
void inventory_snap_selection(int id);
int inventory_command_row();
void set_inventory_command_row(int row);
// Commands an inventory entry offers (0, 1 or 2 rows), by its command kind.
int inventory_command_count(int inventory_idx);
int inventory_slot_count();
int inventory_selected_item();
int inventory_equipped_item();  // inventory index of the equipped weapon
// Packed attack of the equipped weapon (game_state.h player_weapon_attack), and the lowest one a melee weapon has.
int inventory_first_weapon_attack();
// Pad 1 buttons pressed and held this frame (both edges, as a real press reads).
void pad_press_held(std::uint32_t buttons);
// Pad word bits of the left stick pushed left / right (the digital bits a centred analog stick leaves).
std::uint32_t pad_stick_left_bit();
std::uint32_t pad_stick_right_bit();
// A sound effect at `volume` (0..127) and stereo `balance` (-64..63, 0 centre).
void sfx_play_balanced(int sfx_id, int balance, int volume);

// --- Paper maps (the HUD minimap) ---
void set_save_paper_map(int idx);

// --- The game's file queue ---
int file_size(int file_idx);  // bytes, <= 0 when unknown
int file_queue_length();  // reads still queued

// --- 2D HUD prims ---
// The double-buffer index of the frame being built (0 or 1, set when a frame starts).
int frame_buffer_index();
void set_frame_buffer_index(int index);

// --- Maps and their messages ---
// Map registry indices of maps the code names.
namespace map_idx {
enum : int {
    kMap0_S00 = 0, kMap0_S01 = 1, kMap1_S00 = 3, kMap1_S01 = 4, kMap1_S02 = 5, kMap1_S03 = 6, kMap1_S05 = 8,
    kMap2_S00 = 10, kMap2_S01 = 11, kMap2_S04 = 14, kMap3_S03 = 18, kMap5_S02 = 31, kMap5_S03 = 32,
    kMap7_S01 = 40, kMap7_S02 = 41, kMap7_S03 = 42,
    kCount = 45,  // every map index, the two empty test maps included
};
}  // namespace map_idx
// The loaded map overlay's buffer, nullptr before one is loaded (set once at boot).
void* map_overlay_buffer();
void set_map_overlay_buffer(void* buffer);
// The loaded map's message table. The language layer (i18n/localization.h) installs its own table of the map's
// messages in the language in force; the map reads the table in force.
enum class MapMessageCopy : int { None, Lang, Mod };
bool map_header_loaded();
const char** map_messages();       // nullptr without a header
void map_messages_install(MapMessageCopy copy, const char** messages);

}  // namespace sh::game_state
