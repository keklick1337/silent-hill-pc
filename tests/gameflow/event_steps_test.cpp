// Step counters against their specification (the original's state step helpers): what advancing and
// setting each level clears, and the wait/branch/message helpers.

#include <deque>

#include "engine/gameflow/event_steps.h"

#include "framework/sh_test.h"

namespace gf = sh::gameflow;

namespace {

class Steps final : public gf::StepHost {
public:
    std::int32_t s[3] = {0, 0, 0};
    std::int32_t h = 0, t = 0, raw = 0;
    bool stopped = false;
    bool flag_value = false;
    gf::MessageResult msg = gf::MessageResult::Showing;
    std::int32_t step(int l) const override { return s[l]; }
    void store_step(int l, std::int32_t v) override { s[l] = v; }
    std::int32_t hold() const override { return h; }
    void store_hold(std::int32_t v) override { h = v; }
    std::int32_t timer() const override { return t; }
    void store_timer(std::int32_t v) override { t = v; }
    std::int32_t raw_delta() const override { return raw; }
    bool player_stopped() const override { return stopped; }
    bool flag(std::int16_t) const override { return flag_value; }
    gf::MessageResult show_message(int) override { return msg; }
    void fill() { s[0] = 3; s[1] = 4; s[2] = 5; h = 6; t = 7; }
};

}  // namespace

SH_TEST(steps_advance_clears_lower_levels) {
    Steps a;
    a.fill();
    gf::steps::advance(a, 0);
    SH_CHECK(a.s[0] == 4 && a.s[1] == 0 && a.s[2] == 0 && a.h == 0 && a.t == 0);
    a.fill();
    gf::steps::advance(a, 1);
    SH_CHECK(a.s[0] == 3 && a.s[1] == 5 && a.s[2] == 0 && a.h == 6 && a.t == 0);
    a.fill();
    gf::steps::advance(a, 2);
    SH_CHECK(a.s[0] == 3 && a.s[1] == 4 && a.s[2] == 6 && a.h == 6 && a.t == 7);
}

SH_TEST(steps_set_and_finish) {
    Steps a;
    a.fill();
    gf::steps::set(a, 1, 9);
    SH_CHECK(a.s[0] == 3 && a.s[1] == 9 && a.s[2] == 0 && a.h == 6 && a.t == 0);
    a.fill();
    gf::steps::finish(a);
    SH_CHECK(a.s[0] == -1 && a.s[1] == 0 && a.s[2] == 0 && a.h == 0 && a.t == 0);
}

SH_TEST(steps_waits) {
    Steps a;
    a.raw = 2048;
    gf::steps::wait_timer(a, 4096, false);
    SH_CHECK(a.s[0] == 0 && a.t == 2048);
    gf::steps::wait_timer(a, 4096, false);  // 4096 is not past 4096
    SH_CHECK(a.s[0] == 0 && a.t == 4096);
    gf::steps::wait_timer(a, 4096, true);
    SH_CHECK(a.s[1] == 1 && a.t == 0);

    Steps b;
    b.raw = 3000;
    gf::steps::wait_player_stop(b);
    SH_CHECK(b.s[0] == 0);
    gf::steps::wait_player_stop(b);  // 6000 > 1 s limit
    SH_CHECK(b.s[0] == 1);
    b.raw = 0;
    b.stopped = true;
    gf::steps::wait_player_stop(b);
    SH_CHECK(b.s[0] == 2);
}

SH_TEST(steps_branch_and_message) {
    Steps a;
    a.flag_value = true;
    gf::steps::branch_on_flag(a, 10, 7, 8, false);
    SH_CHECK(a.s[0] == 7);
    a.flag_value = false;
    gf::steps::branch_on_flag(a, 10, 7, 8, true);
    SH_CHECK(a.s[1] == 8);

    Steps m;
    gf::steps::message(m, false, 1, 0, 0, 0, false);
    SH_CHECK(m.s[0] == 0);
    m.msg = gf::MessageResult::Finished;
    gf::steps::message(m, false, 1, 0, 0, 0, false);
    SH_CHECK(m.s[0] == 0);
    m.msg = gf::MessageResult::Choice0;
    gf::steps::message(m, false, 1, 0, 0, 0, false);
    SH_CHECK(m.s[0] == 1);
    m.msg = gf::MessageResult::Choice2;
    gf::steps::message(m, true, 1, 11, 12, 13, true);
    SH_CHECK(m.s[1] == 13);
}
