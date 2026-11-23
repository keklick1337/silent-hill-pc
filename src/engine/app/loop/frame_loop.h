// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game's frame loop services on the engine loop classes: the frame clock, the frame
// pacer, the frame profiler, the vertical-blank timers and soft reset watch, the pad tracker and the input gate of
// the overlays, all over sh::game::session() and the frame view (engine/loop/frame_view.h).
#pragma once

#include "engine/loop/frame_profiler.h"

namespace sh::loop {

// --- frame clock and pacer ---
void pace_demo_frame();
void pace_frame();
void clock_demo_frame();
void clock_credit_vblanks();
void clock_line_frame();
void clock_commit();
// The frame rate is not tied to whole vertical blanks (a cap above the blank rate or none): frames may come
// between two updates.
bool frame_rate_free();
// The frame that just updated is one of several updates making up a longer frame: it is not presented, and the
// next update follows at once. update_owed_done() after each such update.
bool update_owed();
void update_owed_done();
// The player controls the game (in game, gameplay state).
bool in_play();

// --- frame profiler ---
void profile_mark(FrameMark mark);
void profile_sample();

// --- vertical-blank timers and the soft reset watch ---
// The vertical-blank interrupt handler of the game.
void vblank_tick();
// Installs vblank_tick on the vertical-blank clock. The handler runs on the clock's thread: the state it touches is
// built here first, and the thread is stopped and joined at exit before any of that state is destroyed (exit()
// runs at-exit handlers and static destructors in reverse order of registration).
void install_vblank_tick();
// The reset combination, the demo timer or a requested reset sends the game back to the title this frame.
bool warm_reset_due();

// --- pads ---
void pad_init();
// The first pad's report of this frame into its state record.
void pad_read();
// A frame presented without an update: the report's presses are kept for the next pad_read.
void pad_latch();
// Both pads' states from their reports.
void pad_build();
// pad_read then pad_build.
void pad_update();
void input_gate_after_pad();

}  // namespace sh::loop
