// The soft reset watch and vertical blank timers of the game loop (engine/loop/reset_watch.h),
// with the PS1 pad bits of the game's combinations.

#include "engine/loop/reset_watch.h"

#include "framework/sh_test.h"

#include <cstring>

namespace {

namespace lp = sh::loop;

constexpr std::uint32_t kSelect = 1u << 0, kStart = 1u << 3, kL2 = 1u << 8, kR2 = 1u << 9, kL1 = 1u << 10,
                        kR1 = 1u << 11, kTriangle = 1u << 12, kSquare = 1u << 15;
constexpr std::uint32_t kAll = kSelect | kStart | kL2 | kR2 | kL1 | kR1;

lp::ResetWatch make() {
    lp::ResetSpec spec;
    spec.hold_buttons = kSelect | kStart;
    spec.hold_vblanks = 120;
    spec.hold_name = "hold";
    spec.demo_vblanks = 1800;
    spec.combos = {{kAll, kAll, "combo"}, {kStart | kTriangle | kSquare, kStart, "alt"}};
    return lp::ResetWatch(spec);
}

bool is(const char* got, const char* want) {
    return got != nullptr && std::strcmp(got, want) == 0;
}

}  // namespace

SH_TEST(loop_reset_hold_and_combos) {
    const lp::ResetWatch w = make();
    std::int32_t demo = 50, hold = 121;
    lp::ResetInput in;
    in.held = kSelect | kStart;
    SH_CHECK(is(w.check(in, {&demo, &hold}), "hold"));
    SH_CHECK(demo == 0);  // no demo: its counter is cleared
    hold = 120;
    SH_CHECK(w.check(in, {&demo, &hold}) == nullptr);
    in.held = kSelect;  // let go: the hold timer starts again
    SH_CHECK(w.check(in, {&demo, &hold}) == nullptr);
    SH_CHECK(hold == 0);
    in.held = kAll;
    in.pressed = kL1;
    SH_CHECK(is(w.check(in, {&demo, &hold}), "combo"));
    in.held = kAll | kTriangle;  // the combination must be held exactly
    SH_CHECK(w.check(in, {&demo, &hold}) == nullptr);
    in.held = kStart | kTriangle | kSquare;
    in.pressed = kTriangle;
    SH_CHECK(w.check(in, {&demo, &hold}) == nullptr);
    in.pressed = kStart;
    SH_CHECK(is(w.check(in, {&demo, &hold}), "alt"));
    in.held = 0;
    in.pressed = 0;
    in.requested = true;
    SH_CHECK(is(w.check(in, {&demo, &hold}), "flag"));
}

SH_TEST(loop_reset_states) {
    const lp::ResetWatch w = make();
    std::int32_t demo = 1801, hold = 500;
    lp::ResetInput in;
    in.held = kSelect | kStart;
    in.demo_playing = true;
    in.blocked = true;
    SH_CHECK(w.check(in, {&demo, &hold}) == nullptr);
    SH_CHECK(demo == 1801);
    in.blocked = false;
    SH_CHECK(is(w.check(in, {&demo, &hold}), "demo-timeout"));
    demo = 1800;
    in.at_main_menu = true;  // the menu: only the demo time-out counts
    SH_CHECK(w.check(in, {&demo, &hold}) == nullptr);
    SH_CHECK(hold == 500);
}

SH_TEST(loop_vblank_timers) {
    std::int32_t a = 0, b = 7;
    lp::VBlankTimers t;
    t.add(&a);
    t.add(&b);
    t.tick();
    t.tick();
    SH_CHECK(a == 2);
    SH_CHECK(b == 9);
}
