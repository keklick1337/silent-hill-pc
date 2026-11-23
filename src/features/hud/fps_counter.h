// SPDX-License-Identifier: GPL-3.0-or-later
//
// The frame-rate counter (config fps_display: the corner, 1..4) and the profiler that replaces its one line with
// frame pacing, processor, graphics card and memory figures (fps_profiler, console `profiler`).
#pragma once

namespace sh::hud {

// Once per presented frame, after the frame has been captured: counts the frame and queues the panel on the
// overlay. Also feeds the always-on performance history.
void draw_fps_counter();

}  // namespace sh::hud
