// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The freeze-frame release rules of the main loop.
// While the pause menu, a map-screen message or a world
// item pickup is up, the renderer presents the captured world frame; this decides, once per tick after
// the state update, when that latch drops and when the pause menu is re-entered or forgotten.
// The pause-menu bookkeeping is reached through FreezeHost because two of its answers are consumed
// (taking the return flag) and must only be asked when the rule before them holds.

#pragma once

namespace sh::gameflow {

class FreezeHost {
public:
    virtual ~FreezeHost() = default;
    virtual bool pause_return_pending() const = 0;
    virtual bool pause_return_take() = 0;  // consumes the flag
    virtual void pause_state_enter() = 0;  // the game's paused system state
    virtual void pause_reset() = 0;
    virtual void pause_away_update() = 0;
};

struct FreezeInputs {
    bool pickup_active = false;          // a world item-pickup model is on screen this tick
    bool in_game = false;                // the 3D-world game state
    bool at_or_below_load_screen = false;  // the title side: the load screen and everything under it
    bool load_screen_or_menu = false;    // exactly the load screen or the main menu
    bool sys_gameplay = false;           // the system state is plain gameplay
    bool pause_flag = false;             // this tick's handler asked for the world draw to be gated off
};

struct FreezeLatch {
    bool present_last_frame = false;   // the renderer presents the captured frame
    bool release_pending = false;      // a handler asked for the freeze to drop on the next drawn tick
};

class FreezeRules {
public:
    // Applies the rules in order and updates `latch`.
    void update(const FreezeInputs& in, FreezeLatch& latch, FreezeHost& host);

private:
    bool pickup_was_ = false;
};

}  // namespace sh::gameflow
