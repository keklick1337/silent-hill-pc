// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The performance run recorder: the part without the game. Frames go
// in, rows come out: one row per window of about a quarter second and one per hitch (a frame far above the running
// average), with the frame-time stability figures first. The CSV text of a row is made here too, so the tests and
// tools/scripts/perf_report.py read one format. The live side (features/dump/perf_recorder.cpp) feeds it from the
// renderer and writes the files.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sh::devtools {

// One presented frame. Negative values are "not measured".
struct PerfRecordFrame {
    double frame_ms = 0.0;                       // present to present
    double gpu_ms = -1.0;                        // GPU time of the command lists that finished since the previous frame
    std::vector<std::pair<int, double>> passes;  // (pass id, ms) of the same command lists, when timed per pass
    double cpu_update_ms = 0.0;                  // the game's update
    double cpu_render_ms = 0.0;                  // building and recording the frame, without the waits below
    double wait_ms = 0.0;                        // frame pacing, submit and present: time the main thread waits
    double draws = 0.0;
    double triangles = 0.0;
    double patch_triangles = -1.0;  // input triangles drawn as tessellation patches
    // What can make one frame long: pipelines compiled on first use, texels uploaded, upscaled pictures swapped
    // in, map cells that became ready.
    double pipelines_built = 0.0;
    double pipeline_build_ms = 0.0;
    double texture_upload_kb = 0.0;
    double upscale_swaps = 0.0;
    double cells_loaded = 0.0;
};

// Where the row was taken and what the renderer had: filled by the live side.
struct PerfRecordContext {
    std::string map;
    int room = -1;
    std::array<double, 3> player{0, 0, 0};  // metres
    double player_yaw = 0.0;                // degrees
    std::array<double, 3> camera{0, 0, 0};  // metres
    double camera_yaw = 0.0;                // degrees
    bool cutscene = false;
    int lights = -1;
    bool rt = false;
    double vram_mb = -1.0;
    int uploads_pending = -1;
    double cpu_main_ms = -1.0;  // the main thread's CPU time per frame over the window (process statistics)
    double gpu_busy_pct = -1.0;
};

struct PerfRecordStats {
    int frames = 0;
    double seconds = 0.0;
    double fps_avg = 0.0;
    double fps_low_1pct = 0.0;  // the rate of the slowest 1% of the frames (at least one frame)
    double frame_ms_avg = 0.0;
    double frame_ms_max = 0.0;
    double frame_ms_stdev = 0.0;
    double jitter_ms = 0.0;  // mean change of the frame time from one frame to the next: uneven pacing
    int hitches = 0;
    double gpu_ms = -1.0;
    double pass_timing = 0.0;     // share of the frames whose GPU time came with per-pass times
    std::vector<double> pass_ms;  // by pass id, mean over the frames that timed it; negative: not seen
    double cpu_update_ms = 0.0, cpu_render_ms = 0.0, wait_ms = 0.0;
    double draws = 0.0, triangles = 0.0, patch_triangles = -1.0;
    // Sums over the row's frames.
    double pipelines_built = 0.0, pipeline_build_ms = 0.0, texture_upload_kb = 0.0, upscale_swaps = 0.0,
           cells_loaded = 0.0;
};

struct PerfRecordRow {
    double time_s = 0.0;  // recorded time at the row's end (hitch: at the hitch frame)
    bool hitch = false;
    PerfRecordStats stats;
    PerfRecordContext context;
};

// The run as a whole, from every frame: what the stop message and the JSON summary say.
struct PerfRecordSummary {
    std::uint64_t frames = 0;
    double seconds = 0.0;
    double fps_avg = 0.0;
    double frame_ms_p50 = 0.0, frame_ms_p99 = 0.0, frame_ms_p999 = 0.0, frame_ms_max = 0.0;
    double frame_ms_stdev = 0.0;
    double jitter_ms = 0.0;
    std::uint64_t hitches = 0;
    // Per-pass timestamps: windows measured with them and without, their mean GPU and frame times.
    int rows_timed = 0, rows_untimed = 0;
    double gpu_ms_timed = -1.0, gpu_ms_untimed = -1.0, frame_ms_timed = -1.0, frame_ms_untimed = -1.0;
};

class PerfRecordCore {
public:
    struct Options {
        double window_seconds = 0.25;
        double hitch_factor = 2.0;     // a frame this many times the running average is a hitch...
        double hitch_floor_ms = 10.0;  // ...when it is also at least this long: shorter frames are not seen
        int hitch_gpu_frames = 2;      // GPU times arrive a frame or two late: a hitch row waits for them
    };

    PerfRecordCore();
    explicit PerfRecordCore(const Options& options);

    // The id of a pass name, its column in the rows; new names get the next id.
    int pass_id(std::string_view name);
    const std::vector<std::string>& pass_names() const { return pass_names_; }

    // What add() saw: the live side fills the context of a window that closed and of a hitch that began.
    struct Added {
        bool window_closed = false;
        bool hitch_began = false;
    };
    Added add(const PerfRecordFrame& frame);

    // The rows finished so far, oldest first; the live side sets the context of the newest before it takes them.
    std::vector<PerfRecordRow>& rows() { return rows_; }
    // The hitch row waiting for its GPU times (null when none); its context is set right after hitch_began.
    PerfRecordRow* pending_hitch() { return hitch_ ? &hitch_row_ : nullptr; }
    // Closes the open window and a waiting hitch as they are (at stop).
    void flush();

    int windows_closed() const { return windows_closed_; }
    double time_s() const { return time_s_; }
    PerfRecordSummary summary() const;

private:
    struct Window {
        int frames = 0;
        double seconds = 0.0;
        double sum_sq = 0.0;
        double max_ms = 0.0;
        double jitter_sum = 0.0;
        int jitter_count = 0;
        int hitches = 0;
        std::vector<double> times;
        double gpu_sum = 0.0;
        int gpu_frames = 0;
        int timed_frames = 0;
        std::vector<double> pass_sum;
        std::vector<int> pass_frames;
        double cpu_update = 0.0, cpu_render = 0.0, wait = 0.0, draws = 0.0, triangles = 0.0, patches = 0.0;
        int patch_frames = 0;
        double pipelines = 0.0, pipeline_ms = 0.0, upload_kb = 0.0, swaps = 0.0, cells = 0.0;
    };
    static void accumulate(Window& w, const PerfRecordFrame& f, double previous_ms);
    PerfRecordStats close(const Window& w) const;
    void finish_hitch();

    Options options_;
    std::vector<std::string> pass_names_;
    Window window_;
    double previous_ms_ = -1.0;
    double average_ms_ = 0.0;
    int seen_ = 0;
    double time_s_ = 0.0;
    int windows_closed_ = 0;
    std::vector<PerfRecordRow> rows_;
    bool hitch_ = false;
    int hitch_wait_ = 0;
    PerfRecordRow hitch_row_;

    // The run: a histogram of frame times for the percentiles (0.1 ms bins up to kBins / 10 ms).
    static constexpr int kBins = 5000;
    std::vector<std::uint32_t> histogram_;
    std::uint64_t frames_ = 0, hitches_ = 0;
    double sum_ = 0.0, sum_sq_ = 0.0, max_ms_ = 0.0, jitter_sum_ = 0.0;
    std::uint64_t jitter_count_ = 0;
    int rows_timed_ = 0, rows_untimed_ = 0;
    double gpu_timed_ = 0.0, gpu_untimed_ = 0.0, frame_timed_ = 0.0, frame_untimed_ = 0.0;
};

// The CSV of the rows: a header from the pass names known so far and one line per row. A row written before a pass
// was first seen has fewer cells; readers take the missing ones as empty.
std::string perf_csv_header(const std::vector<std::string>& pass_names);
std::string perf_csv_line(const PerfRecordRow& row);

// One line of the per-frame file (<name>.frames.csv) and its header.
std::string perf_frames_header();
std::string perf_frames_line(double time_s, const PerfRecordFrame& frame);

// A recording's file name: the given name with only letters, digits, '-', '_' and '.' kept, or empty when nothing
// is left.
std::string perf_record_file_name(std::string_view name);

}  // namespace sh::devtools
