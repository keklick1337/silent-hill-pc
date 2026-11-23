// The pad tracker of the game loop (engine/loop/pad_tracker.h) against its specification,
// with the button layout of the PS1 pad and values worked out by hand.

#include "engine/loop/pad_tracker.h"

#include "framework/sh_test.h"

namespace {

namespace lp = sh::loop;

constexpr std::uint32_t kStart = 1u << 3;
constexpr std::uint32_t kUpPad = 1u << 4, kRightPad = 1u << 5, kDownPad = 1u << 6, kLeftPad = 1u << 7;
constexpr std::uint32_t kCross = 1u << 14, kSquare = 1u << 15;
constexpr std::uint32_t kUp2 = 1u << 16, kRight2 = 1u << 17, kDown2 = 1u << 18, kLeft2 = 1u << 19;
constexpr std::uint32_t kRUp = 1u << 20, kRRight = 1u << 21;
constexpr std::uint32_t kUp = 1u << 24, kRight = 1u << 25, kDown = 1u << 26, kLeft = 1u << 27;

lp::PadSpec spec() {
    lp::PadSpec s;
    s.dpad = {kUpPad, kRightPad, kDownPad, kLeftPad};
    s.left_analog = {kUp2, kRight2, kDown2, kLeft2};
    s.right_analog = {kRUp, kRRight, 1u << 22, 1u << 23};
    s.direction = {kUp, kRight, kDown, kLeft};
    s.dead_zone = 64;
    s.repeat_delay = 30;
    s.repeat_interval = 3;
    s.dpad_stick = 45;
    s.dpad_steer = 32;
    s.dpad_steer_back_walk = -32;
    s.dpad_steer_back_run = -64;
    return s;
}

lp::PadInput digital(std::uint32_t pressed) {
    lp::PadInput in;
    in.connected = true;
    in.buttons_up = static_cast<std::uint16_t>(~pressed & 0xFFFF);
    in.sticks = {128, 128, 128, 128};
    return in;
}

}  // namespace

SH_TEST(loop_pad_edges_and_repeat) {
    const lp::PadTracker t(spec());
    lp::PadState s;
    t.update(s, digital(kCross), 2, false, 0);
    SH_CHECK(s.held == kCross);
    SH_CHECK(s.pressed == kCross);
    SH_CHECK(s.pulsed == kCross);
    SH_CHECK(s.repeat_ticks == 0);
    // Held: no new press, the repeat waits 30 vertical blanks, then repeats every 3.
    for (int i = 0; i < 14; ++i) {
        t.update(s, digital(kCross), 2, false, 0);
        SH_CHECK(s.pressed == 0);
        SH_CHECK(s.pulsed == 0);
    }
    SH_CHECK(s.repeat_ticks == 28);
    t.update(s, digital(kCross), 2, false, 0);
    SH_CHECK(s.pulsed == kCross);
    SH_CHECK(s.repeat_ticks == 27);
    t.update(s, digital(kCross), 2, false, 0);
    SH_CHECK(s.pulsed == 0);
    SH_CHECK(s.repeat_ticks == 29);
    t.update(s, digital(0), 2, false, 0);
    SH_CHECK(s.held == 0);
    SH_CHECK(s.released == kCross);
    SH_CHECK(s.repeat_ticks == 0);
    // A disconnected pad holds nothing whatever it reports.
    lp::PadInput off = digital(kStart);
    off.connected = false;
    t.update(s, off, 1, false, 0);
    SH_CHECK(s.held == 0);
}

SH_TEST(loop_pad_directions) {
    const lp::PadTracker t(spec());
    lp::PadState s;
    t.update(s, digital(kUpPad | kLeftPad), 1, false, 0);
    SH_CHECK(s.held == (kUpPad | kLeftPad | kUp | kLeft));
    // Menus get one axis: vertical wins.
    SH_CHECK(s.pulsed_menu == (kUpPad | kLeftPad | kUp));
    // Opposite d-pad directions cancel in the merged bits only.
    t.update(s, digital(kUpPad | kDownPad), 1, false, 0);
    SH_CHECK(s.held == (kUpPad | kDownPad));
}

SH_TEST(loop_pad_analog_sticks) {
    const lp::PadTracker t(spec());
    lp::PadState s;
    lp::PadInput in = digital(0);
    in.analog = true;
    in.sticks = {255, 128, 0, 192};  // right X full right, left X full left, left Y down at the dead zone edge
    t.update(s, in, 1, false, 0);
    SH_CHECK(s.sticks_raw[lp::kRightX] == 127);
    SH_CHECK(s.sticks_raw[lp::kLeftX] == -128);
    SH_CHECK(s.sticks[lp::kRightX] == 127 - 63);
    SH_CHECK(s.sticks[lp::kLeftX] == -128 + 64);
    SH_CHECK(s.sticks[lp::kLeftY] == 1);
    SH_CHECK(s.sticks[lp::kRightY] == 0);
    SH_CHECK(s.held == (kRRight | kLeft2 | kDown2 | kLeft | kDown));
    SH_CHECK(s.steer == s.sticks);
    // Not in analog mode the sticks read as centred.
    in.analog = false;
    t.update(s, in, 1, false, 0);
    SH_CHECK(s.sticks_raw[lp::kLeftX] == 0);
    SH_CHECK(s.held == 0);
}

SH_TEST(loop_pad_dpad_steering) {
    const lp::PadTracker t(spec());
    lp::PadState s;
    t.update(s, digital(kUpPad | kRightPad), 1, true, kSquare);
    SH_CHECK(s.sticks[lp::kLeftY] == -45);
    SH_CHECK(s.sticks[lp::kLeftX] == 45);
    SH_CHECK(s.steer[lp::kLeftY] == -32);
    SH_CHECK(s.steer[lp::kLeftX] == 32);
    t.update(s, digital(kUpPad | kSquare), 1, true, kSquare);
    SH_CHECK(s.steer[lp::kLeftY] == -64);
    SH_CHECK(s.sticks[lp::kLeftX] == 0);
    t.update(s, digital(kDownPad | kLeftPad), 1, true, kSquare);
    SH_CHECK(s.steer[lp::kLeftY] == 32);
    SH_CHECK(s.steer[lp::kLeftX] == -32);
    // Both vertical directions: no steering on that axis.
    t.update(s, digital(kUpPad | kDownPad), 1, true, kSquare);
    SH_CHECK(s.steer[lp::kLeftY] == 0);
    // A pushed stick wins over the d-pad.
    lp::PadInput in = digital(kUpPad);
    in.analog = true;
    in.sticks = {128, 128, 128, 255};
    t.update(s, in, 1, true, kSquare);
    SH_CHECK(s.sticks[lp::kLeftY] == 64);
    SH_CHECK(s.steer[lp::kLeftY] == 64);
    // Another controller never steers.
    lp::PadState other;
    t.update(other, digital(kUpPad), 1, false, kSquare);
    SH_CHECK(other.steer[lp::kLeftY] == 0);
}
