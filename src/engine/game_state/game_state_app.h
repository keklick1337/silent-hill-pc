// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// What the application layer (boot in engine/app/game_main.cpp, the voice-line player in audio/voice) needs from the
// game's records: the disc file table, the fixed PS1 RAM homes the game expects at boot, the XA voice index, the
// frame interval. Implemented on the game's records (src/engine/game_state/game_state_frames.cpp).

#pragma once

#include "engine/game_state/game_state.h"

#include <cstdint>

namespace sh::game_state {

// The disc file table: fills it from the imported data (and the XA offsets with it). Ends the program with a
// dialog when the game data has no table.
void init_file_table();

// Points the map overlay data and the demo's play buffer at their work buffers (after the buffers are cleared).
void attach_work_buffers();

// The first-person camera's head offset (game units), the baseline the camera starts from.
void set_fps_head_offset(int x, int y, int z);

// XA voice line `xa_index`: the first CD sector of its audio, false when the index has no line on the disc.
bool xa_line_start_sector(int xa_index, int* sector);
// VBlanks per game frame as the game last measured them (its frame interval).
int frame_interval_vblanks();

}  // namespace sh::game_state
