// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/gameflow/freeze_latch.h"

namespace sh::gameflow {

void FreezeRules::update(const FreezeInputs& in, FreezeLatch& latch, FreezeHost& host) {
    // The pickup pass held the latch to show the frozen room behind the isolated model: drop it the
    // instant the pickup stops re-arming.
    if (pickup_was_ && !in.pickup_active) {
        latch.present_last_frame = false;
    }
    pickup_was_ = in.pickup_active;

    // Deferred release of the pause / map-message freeze: those handlers gate the world draw off for a
    // whole tick, so dropping the freeze inside the handler would leave one frame with nothing behind it. Hold
    // it until a tick that actually drew something.
    if (latch.release_pending && (!in.in_game || !in.pause_flag)) {
        latch.present_last_frame = false;
        latch.release_pending = false;
    }

    // Nothing on the title side can legitimately hold the freeze; a route out of a holder that is not
    // its own exit would leave the latch set under the whole title screen.
    if (latch.present_last_frame && in.at_or_below_load_screen) {
        latch.present_last_frame = false;
        latch.release_pending = false;
    }

    // Pause -> OPTION holds the frozen frame through its fade; once another screen takes over the held
    // capture must not be presented under it.
    if (host.pause_return_pending() && !in.in_game) {
        latch.present_last_frame = false;
    }

    // Back from a screen the pause menu opened: taken on that screen's last tick so no hook of the next
    // tick ever sees plain gameplay.
    if (in.in_game && in.sys_gameplay && host.pause_return_take()) {
        host.pause_state_enter();
    }

    // A load that went through, or the way back to the title: nothing is coming back to the pause menu.
    if (in.load_screen_or_menu) {
        host.pause_reset();
    }
    host.pause_away_update();
}

}  // namespace sh::gameflow
