// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Per-frame cheat enforcement and the D-pad movement gate of the main loop. Pure
// rules: the bridge reads the game state into FrameCheatIn and applies FrameCheatOut back.

#pragma once

namespace sh::gameflow {

struct FrameCheatIn {
    bool god_mode = false;
    bool no_target = false;
    bool fast_forward = false;
    bool in_game = false;
    bool game_over = false;      // the system state is the game-over sequence
    bool player_dead = false;    // player health <= 0
};

struct FrameCheatOut {
    bool fast_forward = false;       // new value of the toggle (cleared on death / game over)
    bool refill_health = false;      // set health to the maximum
    bool write_skip_swap = false;    // in game: mirror fast-forward into the vsync-skip flag
    bool skip_swap = false;
    bool hold_no_target = false;     // set the enemy-attack-veto flag and zero its timer
};

FrameCheatOut frame_cheats_apply(const FrameCheatIn& in);

// "Disable D-pad for movement" applies only in plain gameplay with the debug menu closed.
bool dpad_movement_disabled(bool config_on, bool in_game, bool sys_gameplay, bool debug_menu_active);

}  // namespace sh::gameflow
