// The performance recorder's core (engine/devtools/perf_record.h): windows, hitches and their late GPU times, the
// run's percentiles, the timestamp comparison and the CSV.

#include "engine/devtools/perf_record.h"

#include "framework/sh_test.h"

#include <cmath>
#include <string>

namespace {

namespace dt = sh::devtools;

dt::PerfRecordFrame frame(double ms, double gpu = 5.0, bool passes = true) {
    dt::PerfRecordFrame f;
    f.frame_ms = ms;
    f.gpu_ms = gpu;
    if (passes) f.passes = {{0, gpu - 1.0}, {1, 1.0}};
    f.draws = 100;
    f.triangles = 1000;
    return f;
}

bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

int count_cells(const std::string& line) {
    int n = 1;
    for (const char c : line) n += c == ',';
    return n;
}

}  // namespace

SH_TEST(devtools_perf_record_windows) {
    dt::PerfRecordCore core;
    SH_CHECK(core.pass_id("scene") == 0 && core.pass_id("post") == 1 && core.pass_id("scene") == 0);
    int closed = 0;
    for (int i = 0; i < 50; ++i) closed += core.add(frame(10.0)).window_closed ? 1 : 0;
    // 0.25 s windows of 10 ms frames: 25 frames each.
    SH_CHECK(closed == 2);
    SH_CHECK(core.rows().size() == 2);
    const dt::PerfRecordStats& s = core.rows()[0].stats;
    SH_CHECK(s.frames == 25);
    SH_CHECK(near(s.fps_avg, 100.0) && near(s.frame_ms_avg, 10.0) && near(s.frame_ms_max, 10.0));
    SH_CHECK(near(s.jitter_ms, 0.0) && near(s.frame_ms_stdev, 0.0, 1e-4));
    SH_CHECK(near(s.gpu_ms, 5.0) && near(s.pass_timing, 1.0));
    SH_CHECK(s.pass_ms.size() == 2 && near(s.pass_ms[0], 4.0) && near(s.pass_ms[1], 1.0));
    SH_CHECK(near(core.rows()[1].time_s, 0.5, 1e-9));
}

SH_TEST(devtools_perf_record_hitch) {
    dt::PerfRecordCore core;
    core.pass_id("scene");
    core.pass_id("post");
    for (int i = 0; i < 20; ++i) core.add(frame(10.0));
    // A 40 ms frame whose GPU work lands in the frame after it.
    dt::PerfRecordFrame spike = frame(40.0, 5.0);
    spike.pipelines_built = 2;
    spike.pipeline_build_ms = 25.0;
    const dt::PerfRecordCore::Added began = core.add(spike);
    SH_CHECK(began.hitch_began);
    SH_CHECK(core.pending_hitch() != nullptr);
    core.pending_hitch()->context.map = "map0_s01";
    core.add(frame(10.0, 15.0));
    core.add(frame(10.0, 5.0));
    SH_CHECK(core.pending_hitch() == nullptr);
    const dt::PerfRecordRow* hitch = nullptr;
    for (const dt::PerfRecordRow& row : core.rows()) {
        if (row.hitch) hitch = &row;
    }
    SH_CHECK(hitch != nullptr);
    SH_CHECK(hitch->context.map == "map0_s01");
    SH_CHECK(near(hitch->stats.frame_ms_max, 40.0) && hitch->stats.frames == 1);
    SH_CHECK(near(hitch->stats.gpu_ms, 15.0) && near(hitch->stats.pass_ms[0], 14.0));
    SH_CHECK(near(hitch->stats.pipelines_built, 2.0) && near(hitch->stats.pipeline_build_ms, 25.0));
    // Short frames are never hitches, however uneven: below the floor nobody sees them.
    dt::PerfRecordCore fast;
    for (int i = 0; i < 20; ++i) fast.add(frame(2.0));
    SH_CHECK(!fast.add(frame(6.0)).hitch_began);
    // The running average ignores hitches, so two in a row are both found.
    SH_CHECK(core.add(frame(40.0)).hitch_began);
    SH_CHECK(core.add(frame(40.0)).hitch_began);
}

SH_TEST(devtools_perf_record_summary) {
    dt::PerfRecordCore core;
    core.pass_id("scene");
    core.pass_id("post");
    // 1000 frames of 10 ms with 10 of 30 ms: p50 10, p99 10, p99.9 30.
    for (int i = 0; i < 1000; ++i) core.add(frame(i % 100 == 50 ? 30.0 : 10.0));
    const dt::PerfRecordSummary s = core.summary();
    SH_CHECK(s.frames == 1000);
    SH_CHECK(near(s.frame_ms_p50, 10.05, 1e-9) && near(s.frame_ms_p99, 10.05, 1e-9));
    SH_CHECK(near(s.frame_ms_p999, 30.0, 0.06) && near(s.frame_ms_max, 30.0));
    SH_CHECK(s.hitches == 10);
    SH_CHECK(near(s.jitter_ms, 20.0 * 20 / 999.0, 1e-9));
    SH_CHECK(s.rows_timed > 0 && s.rows_untimed == 0);
}

SH_TEST(devtools_perf_record_timestamp_cost) {
    dt::PerfRecordCore core;
    core.pass_id("scene");
    core.pass_id("post");
    // Windows with per-pass times cost 0.5 ms more GPU than windows without.
    for (int w = 0; w < 8; ++w) {
        const bool timed = w % 2 == 0;
        for (int i = 0; i < 25; ++i) core.add(frame(10.0, timed ? 5.5 : 5.0, timed));
    }
    const dt::PerfRecordSummary s = core.summary();
    SH_CHECK(s.rows_timed == 4 && s.rows_untimed == 4);
    SH_CHECK(near(s.gpu_ms_timed - s.gpu_ms_untimed, 0.5, 1e-9));
    SH_CHECK(core.rows()[1].stats.pass_ms[0] < 0.0);
}

SH_TEST(devtools_perf_record_csv) {
    dt::PerfRecordCore core;
    core.pass_id("scene");
    for (int i = 0; i < 25; ++i) core.add(frame(10.0));
    dt::PerfRecordRow row = core.rows().front();
    row.context.map = "map, \"odd\"";
    row.context.player = {-1.5, 0.0, 2.25};
    const std::string header = dt::perf_csv_header(core.pass_names());
    const std::string line = dt::perf_csv_line(row);
    SH_CHECK(header.rfind("time_s,kind,frames,map,", 0) == 0);
    SH_CHECK(header.find(",gpu:scene") != std::string::npos);
    SH_CHECK(line.find("\"map, \"\"odd\"\"\"") != std::string::npos);
    SH_CHECK(line.find(",-1.5,0,2.25,") != std::string::npos);
    // A pass seen later only adds cells at the end: the row has one cell fewer than that header, and the quoted map
    // one comma more.
    SH_CHECK(count_cells(line) == count_cells(dt::perf_csv_header({"scene", "post"})));
    SH_CHECK(line.rfind("0.25,window,25,", 0) == 0);
    const std::string frames = dt::perf_frames_line(1.5, frame(12.5, -1.0));
    SH_CHECK(frames.rfind("1.5,12.5,,", 0) == 0);
    SH_CHECK(count_cells(frames) == count_cells(dt::perf_frames_header()));
    SH_CHECK(dt::perf_record_file_name("../town run 1") == "townrun1");
    SH_CHECK(dt::perf_record_file_name("...").empty());
}
