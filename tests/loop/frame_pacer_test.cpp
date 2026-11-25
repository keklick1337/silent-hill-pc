// The frame pacer of the game loop (engine/loop/frame_pacer.h),
// on a scripted host: a vertical blank counter that advances on every wait and a fake timer.

#include "engine/loop/frame_pacer.h"

#include "framework/sh_test.h"

namespace {

namespace lp = sh::loop;

class FakeHost : public lp::PacerHost {
public:
    int count = 0;
    int waits = 0;
    std::uint64_t ms = 1000;
    std::uint64_t perf = 0;
    std::uint64_t slept = 0;
    bool stalled = false;  // waits do not advance the counter
    int vblank_count() override { return count; }
    void wait_vblank() override {
        ++waits;
        if (!stalled) {
            ++count;
        }
        ms += 16;
    }
    std::uint64_t ticks_ms() override { return ms; }
    std::uint64_t perf_counter() override { return perf += 10; }  // 10 us per read
    std::uint64_t perf_frequency() override { return 1000000; }
    void sleep_ms(std::uint32_t m) override {
        slept += m;
        perf += m * 1000ull;
    }
};

lp::FramePacer make() {
    return lp::FramePacer(lp::PacerSpec{60, 50});
}

}  // namespace

SH_TEST(loop_pacer_target_fps) {
    const lp::FramePacer p = make();
    lp::FpsChoice c;
    c.cap = 30;
    SH_CHECK(p.target_fps(c) == 30);
    c.locked_refresh = 144;
    SH_CHECK(p.target_fps(c) == 144);
    c = {};
    c.cap = 120;
    SH_CHECK(p.target_fps(c) == 120);
    c = {};
    c.cap = 30;
    c.menu = true;  // menus never below 60
    SH_CHECK(p.target_fps(c) == 60);
    c.uncapped = true;
    SH_CHECK(p.target_fps(c) == 0);
}

SH_TEST(loop_pacer_vblank_frames) {
    lp::FramePacer p = make();
    FakeHost h;
    lp::FpsChoice c;
    c.cap = 30;
    // 30 fps: two vertical blanks a frame.
    SH_CHECK(p.capped_frame(h, false, true, 1, c) == 2);
    SH_CHECK(h.count == 2);
    // The game ran three vertical blanks long: nothing to wait.
    h.count += 3;
    SH_CHECK(p.capped_frame(h, false, true, 1, c) == 3);
    SH_CHECK(h.waits == 2);
    // Not capped: the game's own minimum.
    SH_CHECK(p.capped_frame(h, false, false, 1, c) == 1);
    // A fade takes one vertical blank first, which the frame counts.
    SH_CHECK(p.capped_frame(h, true, false, 1, c) == 1);
    // Fixed frame: counts what passed, then waits one.
    h.count += 2;
    SH_CHECK(p.fixed_frame(h, false) == 2);
    // Demo: recorded interval 3, nothing passed yet.
    SH_CHECK(p.demo_frame(h, 3, 1) == 3);
}

SH_TEST(loop_pacer_fixed_backstop) {
    lp::FramePacer p = make();
    FakeHost h;
    h.stalled = true;
    // A stalled counter: the first wait, then the backstop gives up after 50 ms (16 ms per wait).
    SH_CHECK(p.fixed_frame(h, false) == 0);
    SH_CHECK(h.waits == 1 + 4);
    h.waits = 0;
    SH_CHECK(p.fixed_frame(h, true) == 0);
    SH_CHECK(h.waits == 1);
}

SH_TEST(loop_pacer_timer) {
    lp::FramePacer p = make();
    FakeHost h;
    lp::FpsChoice c;
    c.cap = 120;  // 8333 us a frame, on the timer
    SH_CHECK(p.capped_frame(h, false, true, 1, c) == 0);
    SH_CHECK(h.waits == 0);
    SH_CHECK(h.slept == 7);
    SH_CHECK(h.perf >= 8333);
    // 45 fps does not divide 60: also the timer.
    c.cap = 45;
    const std::uint64_t before = h.slept;
    p.capped_frame(h, false, true, 1, c);
    SH_CHECK(h.slept > before);
}

// A display that runs at the cap paces a cap above 60 by itself (FIFO): no timer beats against it. A cap the blanks
// express still waits for them.
SH_TEST(loop_pacer_display_paced) {
    lp::FramePacer p = make();
    FakeHost h;
    lp::FpsChoice c;
    c.cap = 120;
    c.display_paced = true;
    SH_CHECK(p.capped_frame(h, false, true, 1, c) == 0);
    SH_CHECK(h.waits == 0 && h.slept == 0);
    c.cap = 60;
    SH_CHECK(p.capped_frame(h, false, true, 1, c) == 1);
    SH_CHECK(h.waits == 1 && h.slept == 0);
}

// After a frame longer than a blank the platform's wait returns at once (the blank it waits for has passed). Only
// blanks the counter shows are a frame's blanks, so no frame is left with a zero step.
SH_TEST(loop_pacer_counts_only_blanks_that_passed) {
    class LateHost : public FakeHost {
    public:
        int stale = 0;  // waits that return without a blank
        void wait_vblank() override {
            if (stale > 0) {
                --stale;
                ++waits;
                return;
            }
            FakeHost::wait_vblank();
        }
    };
    lp::FramePacer p = make();
    LateHost h;
    lp::FpsChoice c;
    c.cap = 60;
    SH_CHECK(p.capped_frame(h, false, true, 1, c) == 1);
    h.stale = 1;
    SH_CHECK(p.capped_frame(h, false, true, 1, c) == 1);
    SH_CHECK(h.count == 2 && h.waits == 3);
    // The counter and the pacer agree again: the next frame waits one blank.
    SH_CHECK(p.capped_frame(h, false, true, 1, c) == 1);
    SH_CHECK(h.count == 3 && h.waits == 4);
    // A stalled counter is given up on.
    h.stalled = true;
    SH_CHECK(p.capped_frame(h, false, true, 1, c) == 1);
}
