// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The A/B bench of the performance recorder (console `perfbench`):
// at the spot where the player stands, a recording of the settings as they are, then one per variant with one
// graphics key changed (only in the running game, config.cfg keeps its values), then the settings as they are
// again, to see drift. One CSV per step in <exe>/perfrec/bench-<time>/; the table at the end compares them.

#pragma once

#include "features/dump/perf_recorder.h"
#include "formats/common/status.h"

#include <string>
#include <string_view>
#include <vector>

namespace sh::dump {

struct PerfBenchFeature {
    const char* name;  // the variant word of `perfbench`
    const char* key;   // the config key it changes
    float on;          // the value the feature has when on; the variant turns it off when on, on when not
    float off;
};

// The named variants, in the order a bench without variants runs them.
const std::vector<PerfBenchFeature>& perf_bench_features();

// `variants`: feature names or key=value; empty runs every named feature. Fails when one is unknown, a bench or a
// recording runs, or there is no renderer.
Result<std::string> perf_bench_start(double seconds, const std::vector<std::string>& variants, PassTiming timing);
// Stops a running bench and puts the settings back.
Result<std::string> perf_bench_stop();
bool perf_bench_active();
std::string perf_bench_status();

// Once per presented frame, after the recorder took it.
void perf_bench_frame(double frame_ms, bool held);

}  // namespace sh::dump
