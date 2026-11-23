// The input gate of the game loop (engine/loop/input_gate.h):
// the free camera, and the console's swallow until the submit keys are released.

#include "engine/loop/input_gate.h"

#include "framework/sh_test.h"

namespace {

namespace lp = sh::loop;

constexpr std::uint32_t kStart = 1u << 3, kCross = 1u << 14;

lp::PadState pressed(std::uint32_t bits) {
    lp::PadState s;
    s.held = bits;
    s.pressed = bits;
    s.pulsed = bits;
    s.pulsed_menu = bits;
    s.sticks = {1, 2, 3, 4};
    s.sticks_raw = {5, 6, 7, 8};
    s.steer = {9, 9, 9, 9};
    return s;
}

}  // namespace

SH_TEST(loop_gate_console) {
    lp::PadState s = pressed(kStart);
    // Typing: everything goes, sticks too; the swallow state is kept.
    SH_CHECK(!lp::InputGate::console_take(s, true, false, false, kStart));
    SH_CHECK(s.held == 0 && s.sticks[0] == 0 && s.sticks_raw[0] == 0 && s.steer[0] == 9);
    // After typing: held while Start (or a submit key) is still down.
    s = pressed(kStart);
    SH_CHECK(lp::InputGate::console_take(s, false, true, false, kStart));
    SH_CHECK(s.held == 0);
    s = pressed(kCross);
    SH_CHECK(lp::InputGate::console_take(s, false, true, true, kStart));
    // Released: the pad passes.
    s = pressed(kCross);
    SH_CHECK(!lp::InputGate::console_take(s, false, true, false, kStart));
    SH_CHECK(s.held == kCross);
    // Neither: untouched.
    s = pressed(kCross);
    SH_CHECK(!lp::InputGate::console_take(s, false, false, true, kStart));
    SH_CHECK(s.held == kCross);
}

SH_TEST(loop_gate_free_camera) {
    lp::PadState s = pressed(kCross);
    lp::InputGate::free_camera_take(s);
    SH_CHECK(s.held == 0 && s.pressed == 0 && s.released == 0 && s.pulsed == 0 && s.pulsed_menu == 0);
    SH_CHECK(s.sticks[0] == 1);  // the free camera leaves the sticks
}
