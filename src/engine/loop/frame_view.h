// SPDX-License-Identifier: GPL-3.0-or-later
//
// The frame loop's view of the running game: the frame timing values the game reads
// each tick and the vertical-blank counters the reset watch and the demo use. One FrameTiming record owned by
// src/engine/app/game_view.cpp; the accessors below are references into it.
#pragma once

#include <cstdint>

namespace sh::loop {

struct FrameTiming {
    // Vertical blanks credited to this frame (the step of the pad repeat and of the fixed-rate timers).
    std::int32_t vblanks = 0;
    // Vertical blanks that really passed, before the credit limit.
    std::int32_t uncapped_vblanks = 0;
    // The fewest vertical blanks a frame may take (the game sets 1 or 2 per screen).
    std::int32_t interval_vblanks = 1;
    // The simulation step of this frame and its raw (uncapped) value, Q12 seconds; gravity per frame, Q12.
    std::int32_t delta = 0;
    std::int32_t delta_raw = 4096 / 60;  // one 60 Hz tick
    std::int32_t gravity = 0;
    // 1 on the frames that end a 30 Hz game tick of the step (every frame at 30 fps, every other one at 60):
    // the gate of the per-tick random rolls, so their count per tick matches the original at any fps_cap.
    std::int32_t tick30 = 0;
    std::int32_t tick30_accumulator = 0;
    // Above the vertical-blank rate a frame can come before the next blank: no game time passed. Such a frame
    // updates nothing (no game state, no hooks, no random rolls): it presents the last update's lists again, the
    // scene blended between the last two updates. `render_only` is 1 while such a
    // frame runs, `render_only_next` once the clock knows the coming frame is one.
    std::int32_t render_only = 0;
    std::int32_t render_only_next = 0;
    // Frames since boot.
    std::int32_t ticks = 0;
    // Vertical blanks since the demo started, and since the reset combination is held.
    std::int32_t demo_vblanks = 0;
    std::int32_t reset_hold_vblanks = 0;
};

FrameTiming& frame_timing();

inline std::int32_t& frame_vblanks() { return frame_timing().vblanks; }
inline std::int32_t& frame_uncapped_vblanks() { return frame_timing().uncapped_vblanks; }
inline std::int32_t& frame_interval_vblanks() { return frame_timing().interval_vblanks; }
inline std::int32_t& frame_delta() { return frame_timing().delta; }
inline std::int32_t& frame_delta_raw() { return frame_timing().delta_raw; }
inline std::int32_t& frame_gravity() { return frame_timing().gravity; }
inline std::int32_t& frame_ticks() { return frame_timing().ticks; }
inline std::int32_t& demo_vblanks() { return frame_timing().demo_vblanks; }
inline std::int32_t& reset_hold_vblanks() { return frame_timing().reset_hold_vblanks; }

}  // namespace sh::loop
