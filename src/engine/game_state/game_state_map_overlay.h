// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The map registry's view of the game's active map overlay header: which header the game code runs on.
// Implemented on the game's records in src/engine/game_state/game_state_frames.cpp.

namespace sh::game_state {

// Map indices in the game's numbering.
constexpr int kMapIndexFirst = 0;        // map0_s00
constexpr int kMapIndexLastPlayable = 42;  // map7_s03
constexpr int kMapIndexLast = 44;        // the unused test map slot, the last valid overlay id

// Makes `map_index` the current map (what the chunk renderer special-cases).
void set_current_map_index(int map_index);
// Loads the overlay of `map_index` and makes its header the active one; false (header unchanged) on failure.
bool map_overlay_install(int map_index);
// Why the last overlay load failed.
const char* map_overlay_error();
// The active header's map type (its index in the game's map info table).
int map_overlay_map_type();

}  // namespace sh::game_state
