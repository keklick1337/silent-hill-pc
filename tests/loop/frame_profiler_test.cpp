// The frame profiler of the game loop (engine/loop/frame_profiler.h): phases of a slow frame, frames left out of
// the phase sums, and the window summary.
#include "engine/loop/frame_profiler.h"

#include "framework/sh_test.h"

namespace {

namespace lp = sh::loop;

// One frame whose stamps are `base` + the given offsets, sampled at base + sync.
lp::ProfileSample frame(lp::FrameProfiler& p, std::uint32_t base, std::uint32_t top, std::uint32_t upd0,
                        std::uint32_t upd1, std::uint32_t files, std::uint32_t sample, std::uint32_t swap) {
    p.mark(lp::FrameMark::Top, base + top);
    p.mark(lp::FrameMark::UpdateStart, base + upd0);
    p.mark(lp::FrameMark::UpdateEnd, base + upd1);
    p.mark(lp::FrameMark::FilesEnd, base + files);
    const lp::ProfileSample s = p.sample(base + sample, 2);
    p.mark(lp::FrameMark::SwapEnd, base + swap);
    return s;
}

}  // namespace

SH_TEST(loop_profiler_slow_frame) {
    lp::FrameProfiler p;
    SH_CHECK(!frame(p, 1000, 0, 1, 10, 12, 16, 18).slow);  // the first sample only starts the clock
    // 1016 -> 1216: a 200 ms frame, 150 of them in the update.
    const lp::ProfileSample s = frame(p, 1016, 4, 5, 155, 160, 200, 202);
    SH_CHECK(s.slow.has_value());
    SH_CHECK(s.slow->total_ms == 200);
    SH_CHECK(s.slow->phase_ms[lp::kPresent] == 2);   // previous swap end (1018) - previous sample (1016)
    SH_CHECK(s.slow->phase_ms[lp::kTail] == 2);      // top (1020) - swap end
    SH_CHECK(s.slow->phase_ms[lp::kTopOfFrame] == 1);
    SH_CHECK(s.slow->phase_ms[lp::kUpdate] == 150);
    SH_CHECK(s.slow->phase_ms[lp::kFiles] == 5);
    SH_CHECK(s.slow->phase_ms[lp::kSync] == 40);
}

SH_TEST(loop_profiler_window) {
    lp::FrameProfilerSpec spec;
    spec.window_ms = 100;
    lp::FrameProfiler p(spec);
    std::uint32_t t = 1000;
    frame(p, t, 0, 1, 2, 3, 10, 11);
    lp::ProfileSample s;
    for (int i = 0; i < 10 && !s.window; i++) {
        t += 10;
        s = frame(p, t, 1, 2, 7, 8, 10, 11);
    }
    SH_CHECK(s.window.has_value());
    SH_CHECK(s.window->frames == 10);
    SH_CHECK(s.window->average_ms == 10.0);
    SH_CHECK(s.window->vblanks_per_frame == 2.0);
    SH_CHECK(s.window->has_phases);
    SH_CHECK(s.window->phase_ms[lp::kUpdate] == 5.0);
    // A frame with a stamp out of order is left out of the phase sums but counted.
    t += 10;
    p.mark(lp::FrameMark::Top, t + 9);
    p.mark(lp::FrameMark::UpdateStart, t + 2);
    s = p.sample(t + 10, 1);
    SH_CHECK(!s.window);
}
