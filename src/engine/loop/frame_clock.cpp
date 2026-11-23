#include "engine/loop/frame_clock.h"

#include <algorithm>

namespace sh::loop {

namespace {

constexpr float kQ12 = 4096.0f;

// Q12 product, kept in 32 bits like every other step product of the game.
std::int32_t q12_product(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b)) >> 12;
}

// Float to Q12 by truncation; volatile keeps every intermediate in single precision.
std::int32_t to_q12(float value) {
    volatile float scaled = value * kQ12;
    return static_cast<std::int32_t>(scaled);
}

}  // namespace

FrameClock::FrameClock(const ClockSpec& spec) : spec_(spec) {
    lines_per_vblank_ = spec.lines_per_second / spec.vblanks_per_second;
    max_frame_lines_ = spec.lines_per_second / spec.slowest_frame_rate;
    step_lines_ = spec.lines_per_second / spec.step_frame_rate;

    volatile float seconds_per_line = kQ12 / static_cast<float>(spec.lines_per_second);
    seconds_per_line_ = to_q12(seconds_per_line);
    volatile float gravity_per_line = spec.gravity * seconds_per_line;
    gravity_per_line_ = to_q12(gravity_per_line);
    gravity_per_second_ = to_q12(spec.gravity);

    longest_step_ = step_of_lines(step_lines_);
    longest_raw_step_ = step_of_lines(max_frame_lines_);
}

std::int32_t FrameClock::step_of_lines(int lines) const {
    return q12_product(lines, seconds_per_line_);
}

int FrameClock::credited_vblanks(int vblanks, bool player_in_control) const {
    return std::min(vblanks, player_in_control ? spec_.max_vblanks_in_control : spec_.max_vblanks_otherwise);
}

FrameStep FrameClock::demo_step(int present_interval, int elapsed_vblanks) const {
    const int lines = present_interval * lines_per_vblank_;
    FrameStep step;
    step.delta = step_of_lines(lines);
    step.delta_raw = step_of_lines(elapsed_vblanks * lines_per_vblank_);
    step.gravity = q12_product(lines, gravity_per_line_);
    return step;
}

FrameStep FrameClock::line_step(LineCounter& counter) const {
    // The counter is read a second time for the value it is taken at: the counter keeps running,
    // and the frame is measured as late as possible, as the original loop did.
    int lines = counter.read();
    lines = lines < max_frame_lines_ ? counter.read() : max_frame_lines_;
    const int simulated = std::min(lines, step_lines_);

    FrameStep step;
    step.delta = step_of_lines(simulated);
    step.delta_raw = step_of_lines(lines);
    step.gravity = q12_product(simulated, gravity_per_line_);
    return step;
}

FrameStep FrameClock::catch_up(const FrameStep& line, const CatchUpInput& in) {
    if (!wall_clock_seen_) {
        wall_clock_seen_ = true;
        last_wall_clock_ = in.wall_clock;
    }
    std::int32_t passed = static_cast<std::int32_t>(in.wall_clock - last_wall_clock_);
    last_wall_clock_ = in.wall_clock;
    passed = std::max<std::int32_t>(passed, 0);
    if (!in.wall_clock_step) {
        passed = line.delta_raw;
    }

    std::int32_t raw = std::min(passed, longest_raw_step_);
    std::int32_t step = std::min(raw, longest_step_);

    if (in.forget_debt) {
        scene_debt_ = 0;
        passed = step;
    }

    if (in.scene_playing && !in.frozen) {
        scene_debt_ = std::min(scene_debt_ + (passed - step), spec_.scene_debt_limit);
        // Repaying in a frame between updates would make it an update: a scene would update more than once a blank.
        if (scene_debt_ > 0 && step < longest_step_ && (line.delta > 0 || !in.free_rate)) {
            const std::int32_t repaid = std::min(scene_debt_, longest_step_ - step);
            step += repaid;
            scene_debt_ -= repaid;
        }
        raw = step;
    } else if (!in.scene_playing) {
        scene_debt_ = 0;
    }

    FrameStep out;
    out.delta = step;
    out.delta_raw = raw;
    // Gravity keeps the line count's rounding unless the step was changed here.
    out.gravity = step != line.delta ? q12_product(step, gravity_per_second_) : line.gravity;
    return out;
}

bool tick30_step(std::int32_t& accumulator_q12, std::int32_t delta_q12) {
    constexpr std::int32_t kTick = 136;
    constexpr std::int32_t kSlack = kTick / 4;
    if (delta_q12 <= 0) {
        return false;
    }
    accumulator_q12 += delta_q12;
    if (accumulator_q12 < kTick - kSlack) {
        return false;
    }
    accumulator_q12 -= kTick;
    // A long step (a stall, the scene catch-up) is one tick, not a burst of them afterwards.
    if (accumulator_q12 > kTick - kSlack) {
        accumulator_q12 = 0;
    }
    return true;
}

UpdatePlan plan_updates(int passed, int usual_vblanks, int max_vblanks) {
    UpdatePlan plan;
    plan.vblanks = usual_vblanks;
    if (usual_vblanks <= 0 || passed <= usual_vblanks) {
        plan.vblanks = passed;
        return plan;
    }
    plan.updates = std::max(1, std::min(passed, max_vblanks) / usual_vblanks);
    return plan;
}

}  // namespace sh::loop
