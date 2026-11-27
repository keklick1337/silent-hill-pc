// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Menu navigation with repeat and the capture of a new bind (src/input/bind/bind_capture.h).

#include "framework/sh_test.h"

#include "input/bind/bind_capture.h"

#include <array>
#include <string>
#include <vector>

using namespace sh::input;

namespace {

constexpr int kEscape = 41, kCapsLock = 57, kKeyW = 26, kKeyA = 4;

struct Devices {
    std::array<std::uint8_t, 512> keys{};
    CaptureInput at(std::uint32_t now_ms) const {
        CaptureInput in;
        in.now_ms = now_ms;
        in.keys = keys;
        in.mouse_buttons = mouse;
        in.wheel_up = wheel_up;
        in.wheel_down = wheel_down;
        in.pad_buttons = pad;
        in.pad_connected = connected;
        in.key_name = [](int position) -> std::string {
            if (position == kKeyW) return "W";
            if (position == kKeyA) return "A";
            if (position == kCapsLock) return "CapsLock";
            return "";
        };
        return in;
    }
    unsigned mouse = 0;
    bool wheel_up = false, wheel_down = false;
    std::vector<std::string> pad;
    bool connected = true;
};

using Status = BindCapture::Status;

}  // namespace

SH_TEST(menu_nav_first_press_needs_a_release) {
    MenuNav nav;
    NavState held;
    held[NavKey::Ok] = true;  // the press that opened the screen
    SH_CHECK(!nav.step(held, 0)[NavKey::Ok]);
    held[NavKey::Ok] = false;
    SH_CHECK(!nav.step(held, 16)[NavKey::Ok]);
    held[NavKey::Ok] = true;
    SH_CHECK(nav.step(held, 32)[NavKey::Ok]);
    SH_CHECK(!nav.step(held, 48)[NavKey::Ok]);
    // Only the four directions repeat.
    SH_CHECK(!nav.step(held, 5000)[NavKey::Ok]);
}

SH_TEST(menu_nav_directions_repeat) {
    MenuNav nav;
    NavState none, down;
    down[NavKey::Down] = true;
    nav.step(none, 0);
    SH_CHECK(nav.step(down, 1000)[NavKey::Down]);
    SH_CHECK(!nav.step(down, 1359)[NavKey::Down]);
    SH_CHECK(nav.step(down, 1360)[NavKey::Down]);   // after the delay
    SH_CHECK(!nav.step(down, 1449)[NavKey::Down]);
    SH_CHECK(nav.step(down, 1450)[NavKey::Down]);   // then every 90 ms
    SH_CHECK(nav.step(down, 1540)[NavKey::Down]);
    // A direction held through a reset goes on repeating at once; it is the other keys that need a release.
    nav.reset();
    SH_CHECK(nav.step(down, 1541)[NavKey::Down]);
    SH_CHECK(!nav.step(down, 1542)[NavKey::Down]);
}

SH_TEST(bind_capture_waits_for_everything_to_be_released) {
    BindCapture cap;
    Devices d;
    d.keys[kKeyA] = 1;  // still down from choosing the cell
    cap.begin({false, true, true}, CaptureFilter::KeysAndMouse, 0);
    SH_CHECK(cap.step(d.at(0)).status == Status::Waiting);
    SH_CHECK(cap.step(d.at(16)).status == Status::Waiting);
    d.keys[kKeyA] = 0;
    SH_CHECK(cap.step(d.at(32)).status == Status::Waiting);  // armed on this frame
    d.keys[kKeyW] = 1;
    const BindCapture::Result got = cap.step(d.at(48));
    SH_CHECK(got.status == Status::Captured && got.name == "W");
}

SH_TEST(bind_capture_a_lock_that_reads_held_neither_blocks_nor_binds) {
    BindCapture cap;
    Devices d;
    d.keys[kCapsLock] = 1;
    cap.begin({false, false, false}, CaptureFilter::KeysAndMouse, 0);
    SH_CHECK(cap.step(d.at(0)).status == Status::Waiting);   // armed although the lock reads held
    SH_CHECK(cap.step(d.at(16)).status == Status::Waiting);  // and it is not a press
    d.keys[kCapsLock] = 0;
    SH_CHECK(cap.step(d.at(32)).status == Status::Waiting);
    d.keys[kCapsLock] = 1;
    const BindCapture::Result got = cap.step(d.at(48));      // a real press of it is taken
    SH_CHECK(got.status == Status::Captured && got.name == "CapsLock");
}

SH_TEST(bind_capture_escape_cancels) {
    BindCapture cap;
    Devices d;
    cap.begin({false, true, true}, CaptureFilter::KeysAndMouse, 0);
    cap.step(d.at(0));
    d.keys[kEscape] = 1;
    SH_CHECK(cap.step(d.at(16)).status == Status::Cancelled);
}

SH_TEST(bind_capture_mouse_and_wheel_follow_the_cell_and_the_filter) {
    Devices d;
    {
        BindCapture cap;
        cap.begin({false, true, true}, CaptureFilter::KeysAndMouse, 0);
        cap.step(d.at(0));
        d.mouse = 1u << 1;
        const auto got = cap.step(d.at(16));
        SH_CHECK(got.status == Status::Captured && got.name == "Mouse2");
        d.mouse = 0;
    }
    {
        BindCapture cap;  // lowest button first
        cap.begin({false, true, true}, CaptureFilter::MouseOnly, 0);
        cap.step(d.at(0));
        d.keys[kKeyW] = 1;  // a mouse-only capture takes no key
        SH_CHECK(cap.step(d.at(16)).status == Status::Waiting);
        d.mouse = (1u << 4) | (1u << 2);
        const auto got = cap.step(d.at(32));
        SH_CHECK(got.status == Status::Captured && got.name == "Mouse3");
        d.mouse = 0;
        d.keys[kKeyW] = 0;
    }
    {
        BindCapture cap;
        cap.begin({false, true, true}, CaptureFilter::KeysOnly, 0);
        cap.step(d.at(0));
        d.wheel_up = true;
        SH_CHECK(cap.step(d.at(16)).status == Status::Waiting);  // keys only
        d.wheel_up = false;
    }
    {
        BindCapture cap;
        cap.begin({false, true, false}, CaptureFilter::KeysAndMouse, 0);
        cap.step(d.at(0));
        d.wheel_down = true;
        SH_CHECK(cap.step(d.at(16)).status == Status::Waiting);  // the cell takes no wheel
        cap.begin({false, true, true}, CaptureFilter::KeysAndMouse, 0);
        d.wheel_down = false;
        cap.step(d.at(32));
        d.wheel_down = true;
        const auto got = cap.step(d.at(48));
        SH_CHECK(got.status == Status::Captured && got.name == "MouseWheelDown");
        d.wheel_down = false;
    }
    {
        BindCapture cap;
        cap.begin({false, false, false}, CaptureFilter::KeysAndMouse, 0);
        cap.step(d.at(0));
        d.mouse = 1;
        SH_CHECK(cap.step(d.at(16)).status == Status::Waiting);  // the cell takes no mouse
    }
}

SH_TEST(bind_capture_pad_cells) {
    BindCapture cap;
    Devices d;
    d.pad = {"a"};  // the button that chose the cell
    cap.begin({true, false, false}, CaptureFilter::KeysAndMouse, 1000);
    SH_CHECK(cap.step(d.at(1000)).status == Status::Waiting);
    d.pad = {};
    SH_CHECK(cap.step(d.at(1016)).status == Status::Waiting);
    d.pad = {"leftshoulder"};
    SH_CHECK(cap.step(d.at(1032)).status == Status::Waiting);  // taken when it is let go
    d.pad = {};
    const auto got = cap.step(d.at(1048));
    SH_CHECK(got.status == Status::Captured && got.name == "leftshoulder");

    cap.begin({true, false, false}, CaptureFilter::KeysAndMouse, 1000);
    d.pad = {};
    SH_CHECK(cap.step(d.at(9000)).status == Status::Waiting);
    SH_CHECK(cap.step(d.at(9001)).status == Status::Cancelled);  // nothing pressed for 8 seconds

    cap.begin({true, false, false}, CaptureFilter::KeysAndMouse, 0);
    d.connected = false;
    SH_CHECK(cap.step(d.at(16)).status == Status::Cancelled);
    d.connected = true;
    cap.begin({true, false, false}, CaptureFilter::KeysAndMouse, 0);
    d.keys[kEscape] = 1;
    SH_CHECK(cap.step(d.at(16)).status == Status::Cancelled);
}

namespace {

struct PadHeld {
    std::vector<std::string> names;
    CaptureInput at(std::uint32_t now_ms) const {
        CaptureInput in;
        in.now_ms = now_ms;
        in.pad_buttons = names;
        in.pad_connected = true;
        return in;
    }
};

}  // namespace

SH_TEST(bind_capture_pad_takes_what_was_held_when_it_is_let_go) {
    BindCapture cap;
    PadHeld p;
    CaptureTarget chord_cell{true, false, false, true};
    cap.begin(chord_cell, CaptureFilter::KeysAndMouse, 0);
    SH_CHECK(cap.step(p.at(0)).status == Status::Waiting);  // armed: nothing held

    // One button, then a second while the first is held: shown live, taken on release.
    p.names = {"leftshoulder"};
    SH_CHECK(cap.step(p.at(16)).status == Status::Waiting);
    SH_CHECK((cap.pad_held() == std::vector<std::string>{"leftshoulder"}));
    p.names = {"leftshoulder", "rightshoulder"};
    SH_CHECK(cap.step(p.at(300)).status == Status::Waiting);
    SH_CHECK((cap.pad_held() == std::vector<std::string>{"leftshoulder", "rightshoulder"}));
    p.names = {"rightshoulder"};  // let go one at a time
    SH_CHECK(cap.step(p.at(400)).status == Status::Waiting);
    p.names = {};
    auto got = cap.step(p.at(416));
    SH_CHECK(got.status == Status::Captured && got.name == "leftshoulder+rightshoulder");

    // Pressed in the other order, the pair keeps the order of the presses.
    cap.begin(chord_cell, CaptureFilter::KeysAndMouse, 0);
    cap.step(p.at(0));
    p.names = {"rightshoulder"};
    cap.step(p.at(16));
    p.names = {"leftshoulder", "rightshoulder"};  // the devices list the left one first
    cap.step(p.at(32));
    p.names = {};
    got = cap.step(p.at(48));
    SH_CHECK(got.status == Status::Captured && got.name == "rightshoulder+leftshoulder");

    // A single button pressed and let go is that button.
    cap.begin(chord_cell, CaptureFilter::KeysAndMouse, 0);
    cap.step(p.at(0));
    p.names = {"y"};
    cap.step(p.at(16));
    p.names = {};
    got = cap.step(p.at(32));
    SH_CHECK(got.status == Status::Captured && got.name == "y");

    // A cell without combinations takes the first button of the two.
    cap.begin({true, false, false, false}, CaptureFilter::KeysAndMouse, 0);
    cap.step(p.at(0));
    p.names = {"x"};
    cap.step(p.at(16));
    p.names = {"a", "x"};
    cap.step(p.at(32));
    SH_CHECK((cap.pad_held() == std::vector<std::string>{"x"}));
    p.names = {};
    got = cap.step(p.at(48));
    SH_CHECK(got.status == Status::Captured && got.name == "x");

    // Holding a button keeps the capture open past the idle time-out; nothing held for it cancels.
    cap.begin(chord_cell, CaptureFilter::KeysAndMouse, 0);
    cap.step(p.at(0));
    p.names = {"b"};
    SH_CHECK(cap.step(p.at(9000)).status == Status::Waiting);
    p.names = {};
    SH_CHECK(cap.step(p.at(9016)).status == Status::Captured);
    cap.begin(chord_cell, CaptureFilter::KeysAndMouse, 0);
    SH_CHECK(cap.step(p.at(0)).status == Status::Waiting);
    SH_CHECK(cap.step(p.at(8001)).status == Status::Cancelled);
}
