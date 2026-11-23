// The frame clock of the game loop (engine/loop/frame_clock.h) against its specification: the numbers the
// console timing works out to, the step of a demo frame and of a line-counted frame, and the scene catch-up
// with fast-forward, worked out by hand.

#include "engine/loop/frame_clock.h"

#include "framework/sh_test.h"

#include <vector>

namespace {

namespace lp = sh::loop;

lp::ClockSpec console_spec() {
    lp::ClockSpec spec;
    spec.lines_per_second = 15780;
    spec.vblanks_per_second = 60;
    spec.slowest_frame_rate = 15;
    spec.step_frame_rate = 30;
    spec.gravity = 9.8f;
    spec.max_vblanks_in_control = 4;
    spec.max_vblanks_otherwise = 16;
    spec.scene_debt_limit = 2 * 4096;
    return spec;
}

// Returns the given reads in order, then the last one again.
class ScriptedCounter : public lp::LineCounter {
public:
    explicit ScriptedCounter(std::vector<int> reads) : reads_(std::move(reads)) {}
    int read() override {
        const int value = reads_[next_ < reads_.size() ? next_ : reads_.size() - 1];
        ++next_;
        return value;
    }
    std::size_t reads() const { return next_; }

private:
    std::vector<int> reads_;
    std::size_t next_ = 0;
};

}  // namespace

SH_TEST(loop_clock_spec_numbers) {
    const lp::FrameClock clock(console_spec());
    SH_CHECK(clock.lines_per_vblank() == 263);
    SH_CHECK(clock.max_frame_lines() == 1052);
    SH_CHECK(clock.step_lines() == 526);
    SH_CHECK(clock.seconds_per_line() == 1063);
    SH_CHECK(clock.gravity_per_line() == 10419);
    SH_CHECK(clock.gravity_per_second() == 40140);
    SH_CHECK(clock.longest_step() == 136);
    SH_CHECK(clock.longest_raw_step() == 273);
}

SH_TEST(loop_clock_credited_vblanks) {
    const lp::FrameClock clock(console_spec());
    SH_CHECK(clock.credited_vblanks(1, true) == 1);
    SH_CHECK(clock.credited_vblanks(9, true) == 4);
    SH_CHECK(clock.credited_vblanks(9, false) == 9);
    SH_CHECK(clock.credited_vblanks(40, false) == 16);
}

SH_TEST(loop_clock_demo_step) {
    const lp::FrameClock clock(console_spec());
    // Two vertical blanks a frame recorded, three passed: 526 lines simulated, 789 really.
    const lp::FrameStep step = clock.demo_step(2, 3);
    SH_CHECK(step.delta == (526 * 1063) >> 12);
    SH_CHECK(step.delta_raw == (789 * 1063) >> 12);
    SH_CHECK(step.gravity == (526 * 10419) >> 12);
}

SH_TEST(loop_clock_line_step) {
    const lp::FrameClock clock(console_spec());
    {
        // A 60 fps frame: the second read is the value.
        ScriptedCounter counter({263, 264});
        const lp::FrameStep step = clock.line_step(counter);
        SH_CHECK(counter.reads() == 2);
        SH_CHECK(step.delta == (264 * 1063) >> 12);
        SH_CHECK(step.delta_raw == step.delta);
        SH_CHECK(step.gravity == (264 * 10419) >> 12);
    }
    {
        // A 20 fps frame: simulated as a 30 fps step, the raw step keeps all 789 lines.
        ScriptedCounter counter({789, 789});
        const lp::FrameStep step = clock.line_step(counter);
        SH_CHECK(step.delta == 136);
        SH_CHECK(step.delta_raw == (789 * 1063) >> 12);
        SH_CHECK(step.gravity == (526 * 10419) >> 12);
    }
    {
        // Slower than 15 fps: read once, counted as 1052 lines.
        ScriptedCounter counter({5000, 7});
        const lp::FrameStep step = clock.line_step(counter);
        SH_CHECK(counter.reads() == 1);
        SH_CHECK(step.delta_raw == 273);
        SH_CHECK(step.delta == 136);
    }
}

SH_TEST(loop_clock_catch_up_plain) {
    lp::FrameClock clock(console_spec());
    const lp::FrameStep line{68, 68, 669};
    lp::CatchUpInput in;
    in.wall_clock = 1000;
    // Outside a scene and without fast-forward the line step passes through unchanged.
    SH_CHECK(clock.catch_up(line, in) == line);
    in.wall_clock = 5000;
    SH_CHECK(clock.catch_up(line, in) == line);
    // A raw step above the 15 fps floor is clamped to it, the step to the 30 fps one.
    const lp::FrameStep slow{136, 400, 1338};
    const lp::FrameStep out = clock.catch_up(slow, in);
    SH_CHECK(out.delta == 136);
    SH_CHECK(out.delta_raw == 273);
    SH_CHECK(out.gravity == 1338);
}

SH_TEST(loop_clock_catch_up_scene_debt) {
    lp::FrameClock clock(console_spec());
    lp::CatchUpInput in;
    in.scene_playing = true;
    // A 10 fps frame in a scene: 136 simulated, the rest of the clamped raw step (273) is owed.
    lp::FrameStep out = clock.catch_up({136, 400, 1338}, in);
    SH_CHECK(out.delta == 136);
    SH_CHECK(out.delta_raw == 136);
    SH_CHECK(clock.scene_debt() == 400 - 136);
    // A fast frame repays up to the 30 fps step; gravity follows the changed step.
    out = clock.catch_up({68, 68, 669}, in);
    SH_CHECK(out.delta == 136);
    SH_CHECK(out.delta_raw == 136);
    SH_CHECK(out.gravity == (136 * 40140) >> 12);
    SH_CHECK(clock.scene_debt() == 264 - 68);
    // Frozen: nothing is owed or repaid, the raw step is left alone.
    in.frozen = true;
    out = clock.catch_up({100, 100, 984}, in);
    SH_CHECK(out.delta == 100);
    SH_CHECK(out.delta_raw == 100);
    SH_CHECK(out.gravity == 984);
    SH_CHECK(clock.scene_debt() == 196);
    // A blocking movie ended: the debt goes, this frame owes nothing either.
    in.frozen = false;
    in.forget_debt = true;
    out = clock.catch_up({136, 273, 1338}, in);
    SH_CHECK(out.delta == 136);
    SH_CHECK(clock.scene_debt() == 0);
    // The end of the scene drops what is left.
    in.forget_debt = false;
    clock.catch_up({136, 273, 1338}, in);
    SH_CHECK(clock.scene_debt() == 137);
    in.scene_playing = false;
    clock.catch_up({68, 68, 669}, in);
    SH_CHECK(clock.scene_debt() == 0);
}

SH_TEST(loop_clock_catch_up_scene_debt_between_updates) {
    lp::FrameClock clock(console_spec());
    lp::CatchUpInput in;
    in.scene_playing = true;
    in.free_rate = true;
    clock.catch_up({136, 400, 1338}, in);
    SH_CHECK(clock.scene_debt() == 264);
    // Above 60 fps a frame without a step only presents; the debt waits for the next update.
    lp::FrameStep out = clock.catch_up({0, 0, 0}, in);
    SH_CHECK(out.delta == 0);
    SH_CHECK(clock.scene_debt() == 264);
    out = clock.catch_up({68, 68, 669}, in);
    SH_CHECK(out.delta == 136);
    SH_CHECK(clock.scene_debt() == 264 - 68);
}

SH_TEST(loop_clock_catch_up_debt_limit) {
    lp::FrameClock clock(console_spec());
    lp::CatchUpInput in;
    in.scene_playing = true;
    in.wall_clock_step = true;
    in.wall_clock = 0;
    clock.catch_up({136, 136, 1338}, in);
    // Fast-forward: the step is the wall clock's; ten seconds owe at most the limit.
    in.wall_clock = 10 * 4096;
    clock.catch_up({136, 136, 1338}, in);
    SH_CHECK(clock.scene_debt() == 2 * 4096);
    // The wall clock going backwards counts as no time.
    in.wall_clock = 5 * 4096;
    const lp::FrameStep out = clock.catch_up({136, 136, 1338}, in);
    SH_CHECK(out.delta == 136);
    SH_CHECK(clock.scene_debt() == 2 * 4096 - 136);
}

// The 30 Hz tick gate of the per-tick random rolls: every 30 fps frame (a line short too), every other 60 fps
// frame, none while the step is held, and a long step is one tick.
SH_TEST(loop_clock_tick30_gate) {
    std::int32_t acc = 0;
    for (int i = 0; i < 10; ++i) {
        SH_CHECK(lp::tick30_step(acc, 136));
    }
    SH_CHECK(lp::tick30_step(acc, 135) && lp::tick30_step(acc, 137) && lp::tick30_step(acc, 136));
    acc = 0;
    int ticks = 0;
    for (int i = 0; i < 60; ++i) {
        ticks += lp::tick30_step(acc, 68) ? 1 : 0;
    }
    SH_CHECK(ticks == 30);
    SH_CHECK(!lp::tick30_step(acc, 0));
    acc = 0;
    SH_CHECK(lp::tick30_step(acc, 1338) && acc == 0);
}

// A frame longer than usual is made up by updates of the usual step.
SH_TEST(loop_clock_plan_updates) {
    namespace lp = sh::loop;
    // 60 fps and above: one blank a frame. On time: one update of what passed.
    SH_CHECK(lp::plan_updates(1, 1, 8).updates == 1 && lp::plan_updates(1, 1, 8).vblanks == 1);
    SH_CHECK(lp::plan_updates(0, 1, 8).updates == 1 && lp::plan_updates(0, 1, 8).vblanks == 0);
    // Three blanks: three updates of one blank each.
    SH_CHECK(lp::plan_updates(3, 1, 8).updates == 3 && lp::plan_updates(3, 1, 8).vblanks == 1);
    // A long stall is made up to the limit, the rest is dropped.
    SH_CHECK(lp::plan_updates(40, 1, 8).updates == 8);
    // 30 fps: two blanks a frame. One blank late is still one update of the usual two; four blanks are two updates.
    SH_CHECK(lp::plan_updates(2, 2, 8).updates == 1 && lp::plan_updates(2, 2, 8).vblanks == 2);
    SH_CHECK(lp::plan_updates(3, 2, 8).updates == 1 && lp::plan_updates(3, 2, 8).vblanks == 2);
    SH_CHECK(lp::plan_updates(4, 2, 8).updates == 2 && lp::plan_updates(4, 2, 8).vblanks == 2);
    SH_CHECK(lp::plan_updates(100, 2, 8).updates == 4);
    // Not a capped frame: as it came.
    SH_CHECK(lp::plan_updates(5, 0, 8).updates == 1 && lp::plan_updates(5, 0, 8).vblanks == 5);
}
