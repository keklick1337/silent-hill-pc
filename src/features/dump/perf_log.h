// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The [PERF] lines of the log, readable where there is no console (a phone's `adb logcat -s SilentHillPC`): every
// 5 s on a phone or tablet, or with the profiler overlay on, the frame rate, the GPU time and what the frames'
// commands did (rhi::ProfileStats::Counts: render passes, attachment loads and stores, transfers, binds). A file
// named perf_capture in the game's folder starts a detailed capture of 10 s: per-pass GPU times and the timed
// sections inside the scene pass, a trace of two frames' commands, then one summary line per pass (BUILDING.md 5.4).

#pragma once

#include "render/rhi/rhi.h"

#include <string>

namespace sh::dump {

// Once per presented frame, with the device's counters of that frame and the frame's time.
void perf_log_frame(const sh::rhi::ProfileStats& stats, double frame_ms);

// A capture is running: pass timing is on for it.
bool perf_capture_active();

// The capture's progress for the profiler overlay ("" when none runs).
std::string perf_capture_brief();

}  // namespace sh::dump
