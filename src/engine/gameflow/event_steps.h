#pragma once

// Step counters of the running state. States and map event
// scripts are written as sequences: a main step, a sub-step and an "auto" step, plus a hold counter
// and a timer. A script is called every frame, switches on its step and "waits" by returning
// without advancing it. Advancing or setting a level clears the levels below it and the timers that
// belong to them.

#include <cstdint>

#include "engine/gameflow/game_states.h"

namespace sh::gameflow {

class StepHost {
public:
    virtual ~StepHost() = default;

    // level 0 = main step, 1 = sub-step, 2 = auto step.
    virtual std::int32_t step(int level) const = 0;
    virtual void store_step(int level, std::int32_t value) = 0;
    virtual std::int32_t hold() const = 0;             // a frame counter; cleared with the main step
    virtual void store_hold(std::int32_t value) = 0;
    virtual std::int32_t timer() const = 0;            // Q12 seconds; cleared with the main and sub-step
    virtual void store_timer(std::int32_t value) = 0;

    virtual std::int32_t raw_delta() const = 0;
    virtual bool player_stopped() const = 0;
    virtual bool flag(std::int16_t flag) const = 0;
    virtual MessageResult show_message(int message) = 0;
};

namespace steps {

// Longest wait for the player to stop (Q12 seconds).
constexpr std::int32_t kStopWaitLimit = 4096;

void advance(StepHost& h, int level);
void set(StepHost& h, int level, std::int32_t value);
// The main step is finished: the next frame runs the state's closing branch.
void finish(StepHost& h);

// Advances the main step once the player stands still (or after kStopWaitLimit).
void wait_player_stop(StepHost& h);
// Advances the main step (or the sub-step) once `delay` (Q12 seconds) has passed.
void wait_timer(StepHost& h, std::int32_t delay, bool sub);
// Sets the main step (or the sub-step) to `if_set` or `if_clear` by a flag.
void branch_on_flag(StepHost& h, std::int16_t flag, std::int32_t if_set, std::int32_t if_clear, bool sub);
// Shows a message; once it is closed, advances (no choice) or jumps to the step of the chosen entry.
void message(StepHost& h, bool has_choice, int message, std::int32_t step0, std::int32_t step1, std::int32_t step2,
             bool sub);

}  // namespace steps
}  // namespace sh::gameflow
