// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The performance run recorder: console `perfrec`, the Debug
// Options row and the config key perf_record. While it runs, every presented frame goes into
// sh::devtools::PerfRecordCore and its rows into <exe>/perfrec/<name>.csv, with <name>.frames.csv (one line per
// frame) and <name>.json (the build, the GPU, the renderer and every graphics key; at stop also the summary).
// tools/scripts/perf_report.py reads them. `perfbench` (perf_bench.h) records one file per setting variant.

#pragma once

#include "engine/devtools/perf_record.h"
#include "formats/common/status.h"

#include <string>
#include <string_view>

namespace sh::dump {

// How the GPU is timed while recording. Per-pass timestamps tell which pass costs what but can serialise the GPU;
// Sampled takes them three windows in four and measures what they cost from the windows without them.
enum class PassTiming { All, Sampled, Off };

struct PerfRecordRequest {
    std::string name;    // file name without extension; empty: the local time
    std::string folder;  // under <exe>/perfrec/; empty: perfrec itself
    PassTiming timing = PassTiming::Sampled;
    std::string note;  // kept in the JSON (perfbench: the variant)
};

// Starts a recording; the path of the CSV, or why not (one is running, the folder cannot be made).
Result<std::string> perf_record_start(const PerfRecordRequest& request);
// Stops it and writes the summary; the lines to show the player. Nothing running is an error.
Result<std::string> perf_record_stop();
bool perf_record_active();
// The summary of the recording stopped last.
const sh::devtools::PerfRecordSummary& perf_record_last_summary();
// One line: the file, the time recorded and the frames.
std::string perf_record_status();
// The figures of the running recording so far (frame-time stability first); empty when none runs.
std::string perf_record_brief();

// Once per presented frame from the performance history (perf_frame): `frame_ms` present to present, `held` while
// the console or the quick options hold the game (not recorded).
void perf_record_frame(double frame_ms, bool held);

PassTiming pass_timing_from_name(std::string_view name, bool* known);
const char* pass_timing_name(PassTiming timing);

}  // namespace sh::dump
