// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The live side of the performance history (engine/devtools/perf_monitor.h): closes a row of
// the history on every presented frame with the renderer's counters and reads the process's load twice a second.
// Always on, so `dump` and the profiler overlay read the same numbers.

#pragma once

#include "engine/devtools/state_dump.h"
#include "render/rhi/rhi.h"

namespace sh::dump {

// Once per presented frame (the overlay hook).
void perf_frame();

// The device's counters of the frame perf_frame() closed last: the device hands them out once, here.
const sh::rhi::ProfileStats& perf_frame_stats();

// The history and the system's numbers, for dump.json. The game's own counts (characters, effects) are the caller's.
sh::devtools::DumpPerformance perf_snapshot();

}  // namespace sh::dump
