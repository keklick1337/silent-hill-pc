#pragma once

// Frame clock of the game loop: turns what a frame took into the
// simulation step every update reads. The step is in Q12 seconds and is measured in display lines
// of the console the game was written for: a frame lasts as many lines as the line counter counted
// since the previous frame, a slow frame is clamped so that one step never sweeps further than the
// game was tuned for, and gravity gets its own step so that it keeps the rounding of a line count.
//
// On top of that sit two things a PC needs: an authored scene that lost time on slow frames may
// win it back on later fast ones (the scene stays in step with its voices), and fast-forward takes
// the step from the wall clock instead of the line counter.
//
// The game decides all numbers (ClockSpec) and all conditions (CatchUpInput); nothing here knows
// about the game's states.
//
// Bit-exactness contract: with the same line counter reads, wall clock reads and conditions, the
// steps equal the original's to the last bit.

#include <cstdint>

namespace sh::loop {

// The console timing the clock counts in, supplied by the game.
struct ClockSpec {
    int lines_per_second = 0;    // display lines counted per second
    int vblanks_per_second = 0;  // vertical blanks per second
    int slowest_frame_rate = 0;  // frames per second below which a frame is counted as this long
    int step_frame_rate = 0;     // frames per second of the longest simulation step
    float gravity = 0.0f;        // gravity in units per second squared
    int max_vblanks_in_control = 0;  // longest frame in vertical blanks while the player is in control
    int max_vblanks_otherwise = 0;   // ... and while the player is not (scenes, menus, messages)
    std::int32_t scene_debt_limit = 0;  // Q12 seconds a scene may owe itself at most
};

// One frame's simulation step.
struct FrameStep {
    std::int32_t delta = 0;      // Q12 seconds the simulation advances
    std::int32_t delta_raw = 0;  // Q12 seconds that really passed (timers, subtitles), not clamped to the step
    std::int32_t gravity = 0;    // Q12 gravity times the step

    friend bool operator==(const FrameStep&, const FrameStep&) = default;
};

// The display line counter as the clock reads it. A read may return more than the one before it.
class LineCounter {
public:
    virtual ~LineCounter() = default;
    virtual int read() = 0;
};

// The conditions of one frame for FrameClock::catch_up.
struct CatchUpInput {
    std::int64_t wall_clock = 0;  // Q12 seconds of the wall clock, read once every frame
    bool wall_clock_step = false;  // fast-forward: the step is the wall clock's, not the line counter's
    bool scene_playing = false;    // an authored scene is on screen: it may run fast to repay lost time
    bool frozen = false;           // the game is held (console, quick options): a scene owes nothing
    bool forget_debt = false;      // a blocking movie just ended: its run time is not lost scene time
    bool free_rate = false;        // frames come faster than updates: a frame without a line step only presents
};

class FrameClock {
public:
    explicit FrameClock(const ClockSpec& spec);

    // Vertical blanks the frame is credited with: a slow frame counts as the longest the game allows.
    int credited_vblanks(int vblanks, bool player_in_control) const;

    // A demo plays back at the pace it was recorded with: `present_interval` vertical blanks a frame,
    // whatever the frame took (`elapsed_vblanks`, which only the raw step sees).
    FrameStep demo_step(int present_interval, int elapsed_vblanks) const;

    // The step of a frame from the line counter.
    FrameStep line_step(LineCounter& counter) const;

    // The step the simulation takes after the scene catch-up and fast-forward, from the step of the
    // line counter. Keeps the wall clock reading and the scene's debt for the next frame.
    FrameStep catch_up(const FrameStep& line, const CatchUpInput& in);

    // What the spec works out to (Q12 unless stated otherwise).
    int lines_per_vblank() const { return lines_per_vblank_; }
    int max_frame_lines() const { return max_frame_lines_; }
    int step_lines() const { return step_lines_; }
    std::int32_t seconds_per_line() const { return seconds_per_line_; }
    std::int32_t gravity_per_line() const { return gravity_per_line_; }
    std::int32_t gravity_per_second() const { return gravity_per_second_; }
    std::int32_t longest_step() const { return longest_step_; }
    std::int32_t longest_raw_step() const { return longest_raw_step_; }
    std::int32_t scene_debt() const { return scene_debt_; }

private:
    std::int32_t step_of_lines(int lines) const;

    ClockSpec spec_;
    int lines_per_vblank_ = 0;
    int max_frame_lines_ = 0;
    int step_lines_ = 0;
    std::int32_t seconds_per_line_ = 0;
    std::int32_t gravity_per_line_ = 0;
    std::int32_t gravity_per_second_ = 0;
    std::int32_t longest_step_ = 0;
    std::int32_t longest_raw_step_ = 0;

    bool wall_clock_seen_ = false;
    std::int64_t last_wall_clock_ = 0;
    std::int32_t scene_debt_ = 0;
};

// Whether this frame's step ends a 30 Hz game tick (Q12 seconds, 136 a tick). The original rolls its per-tick
// chances once per 30 fps frame; at a higher frame rate the rolls run only on the frame a tick completes, so the
// random number generator is called as often per tick as on the console. A quarter tick of slack keeps a
// 30 fps frame a line short of 136 a tick, as on the console; the accumulator carries the rest.
bool tick30_step(std::int32_t& accumulator_q12, std::int32_t delta_q12);

// A frame that took longer than its usual vertical blanks is made up by several updates of the usual step, not one
// longer step: the game then goes through the same updates as on a machine that never stalled. `usual_vblanks` is
// what one frame takes at the chosen rate (1 at 60 fps and above, 2 at 30), `passed` what this one took. At most
// `max_vblanks` are made up; the rest of a long stall (a loaded machine, a switched-away window) is dropped.
struct UpdatePlan {
    int updates = 1;         // updates to run for this frame, each of the usual step
    int vblanks = 0;         // vertical blanks of each
};
UpdatePlan plan_updates(int passed, int usual_vblanks, int max_vblanks);

}  // namespace sh::loop
