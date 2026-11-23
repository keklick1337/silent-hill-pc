#include "engine/gameflow/event_steps.h"

namespace sh::gameflow::steps {

namespace {

void clear_below(StepHost& h, int level) {
    if (level == 0) {
        h.store_hold(0);
        h.store_step(1, 0);
    }
    if (level <= 1) {
        h.store_timer(0);
        h.store_step(2, 0);
    }
}

}  // namespace

void advance(StepHost& h, int level) {
    const int l = level == 0 ? 0 : level == 1 ? 1 : 2;
    clear_below(h, l);
    h.store_step(l, h.step(l) + 1);
}

void set(StepHost& h, int level, std::int32_t value) {
    const int l = level == 0 ? 0 : level == 1 ? 1 : 2;
    h.store_step(l, value);
    clear_below(h, l);
}

void finish(StepHost& h) { set(h, 0, -1); }

void wait_player_stop(StepHost& h) {
    h.store_timer(h.timer() + h.raw_delta());
    if (h.player_stopped() || h.timer() > kStopWaitLimit) {
        advance(h, 0);
    }
}

void wait_timer(StepHost& h, std::int32_t delay, bool sub) {
    h.store_timer(h.timer() + h.raw_delta());
    if (delay < h.timer()) {
        advance(h, sub ? 1 : 0);
    }
}

void branch_on_flag(StepHost& h, std::int16_t flag, std::int32_t if_set, std::int32_t if_clear, bool sub) {
    set(h, sub ? 1 : 0, h.flag(flag) ? if_set : if_clear);
}

void message(StepHost& h, bool has_choice, int message, std::int32_t step0, std::int32_t step1, std::int32_t step2,
             bool sub) {
    const MessageResult r = h.show_message(message);
    if (r <= MessageResult::Showing) {
        return;
    }
    const int level = sub ? 1 : 0;
    if (!has_choice) {
        advance(h, level);
        return;
    }
    if (r == MessageResult::Choice0) {
        set(h, level, step0);
    }
    if (r == MessageResult::Choice1) {
        set(h, level, step1);
    }
    if (r == MessageResult::Choice2) {
        set(h, level, step2);
    }
}

}  // namespace sh::gameflow::steps
