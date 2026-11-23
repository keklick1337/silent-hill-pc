// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/gameflow/frame_cheats.h"

namespace sh::gameflow {

FrameCheatOut frame_cheats_apply(const FrameCheatIn& in) {
    FrameCheatOut out;
    out.fast_forward = in.fast_forward;
    // Fast-forward must not survive the player's death; checked outside the in-game guard.
    if (out.fast_forward && (in.player_dead || in.game_over)) out.fast_forward = false;
    if (in.in_game) {
        out.refill_health = in.god_mode;
        out.write_skip_swap = true;
        out.skip_swap = out.fast_forward;
        out.hold_no_target = in.no_target;
    }
    return out;
}

bool dpad_movement_disabled(bool config_on, bool in_game, bool sys_gameplay, bool debug_menu_active) {
    return config_on && in_game && sys_gameplay && !debug_menu_active;
}

}  // namespace sh::gameflow
