// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Controller binds of two buttons together (src/input/bind/pad_chord.h): the value, and the resolver that tells the
// combination from its buttons alone.

#include "framework/sh_test.h"

#include "input/bind/pad_chord.h"

#include <set>
#include <string>
#include <vector>

using namespace sh::input;

namespace {

constexpr int kL1 = 9, kR1 = 10, kX = 2;

int lookup(const std::string& name) {
    if (name == "leftshoulder") return kL1;
    if (name == "rightshoulder") return kR1;
    if (name == "x") return kX;
    return -1;
}

// The default quick actions on a pad: reload on R1, the quick turn on L1 + R1, the flashlight on X.
enum : std::size_t { kReload, kTurn, kLight };

struct Pad {
    ChordResolver r;
    std::set<int> down;
    std::uint32_t now = 0;

    explicit Pad(const char* reload = "rightshoulder", const char* turn = "leftshoulder+rightshoulder") {
        r.set_binds({parse_pad_bind(reload, lookup), parse_pad_bind(turn, lookup), parse_pad_bind("x", lookup)});
    }
    // One frame `ms` after the last one.
    void frame(std::uint32_t ms) {
        now += ms;
        r.step(now, [this](int b) { return down.count(b) != 0; });
    }
};

}  // namespace

SH_TEST(pad_chord_values) {
    SH_CHECK((chord_parts("leftshoulder+rightshoulder") == std::vector<std::string>{"leftshoulder", "rightshoulder"}));
    SH_CHECK(
        (chord_parts(" leftshoulder + rightshoulder ") == std::vector<std::string>{"leftshoulder", "rightshoulder"}));
    SH_CHECK((chord_parts("a") == std::vector<std::string>{"a"}));
    SH_CHECK(chord_parts("NONE").empty() && chord_parts("").empty() && chord_parts("none").empty());
    SH_CHECK((chord_parts("a+b+c") == std::vector<std::string>{"a", "b"}));  // two at most
    SH_CHECK(is_chord("a+b") && !is_chord("a") && !is_chord("NONE") && !is_chord("-leftx"));
    const std::vector<std::string> pair{"leftshoulder", "rightshoulder"};
    SH_CHECK(chord_value(pair) == "leftshoulder+rightshoulder");
    SH_CHECK(chord_value(std::vector<std::string>{}) == "NONE");
    SH_CHECK(chord_value(chord_parts("leftshoulder+rightshoulder")) == "leftshoulder+rightshoulder");

    // An old single-button value reads as it always did.
    const PadBind single = parse_pad_bind("leftshoulder", lookup);
    SH_CHECK(single.count == 1 && single.buttons[0] == kL1 && !single.chord());
    const PadBind both = parse_pad_bind("leftshoulder+rightshoulder", lookup);
    SH_CHECK(both.chord() && both.buttons[0] == kL1 && both.buttons[1] == kR1);
    // Half a combination never binds one button alone.
    SH_CHECK(!parse_pad_bind("leftshoulder+nosuch", lookup).bound());
    SH_CHECK(!parse_pad_bind("leftshoulder+leftshoulder", lookup).bound());
    SH_CHECK(!parse_pad_bind("NONE", lookup).bound());
}

SH_TEST(pad_chord_both_together_fire_the_combination_only) {
    Pad p;
    p.frame(16);
    p.down = {kL1, kR1};  // the same frame
    p.frame(16);
    SH_CHECK(p.r.clicked(kTurn) && !p.r.clicked(kReload));
    SH_CHECK(p.r.held(kTurn) && !p.r.held(kReload));
    // Held on, nothing fires again, and R1's reload never comes.
    for (int i = 0; i < 30; ++i) {
        p.frame(16);
        SH_CHECK(!p.r.clicked(kTurn) && !p.r.clicked(kReload) && !p.r.held(kReload));
    }
    p.down.clear();
    p.frame(16);
    SH_CHECK(!p.r.held(kTurn));
}

SH_TEST(pad_chord_grace_window) {
    // L1, then R1 150 ms later: the quick turn, no reload first or after.
    {
        Pad p;
        p.frame(16);
        p.down = {kL1};
        p.frame(16);
        for (int t = 0; t < 140; t += 10) {
            p.frame(10);
            SH_CHECK(!p.r.clicked(kTurn) && !p.r.clicked(kReload));
        }
        p.down = {kL1, kR1};
        p.frame(10);
        SH_CHECK(p.r.clicked(kTurn) && !p.r.clicked(kReload));
        for (int t = 0; t < 400; t += 16) {
            p.frame(16);
            SH_CHECK(!p.r.clicked(kReload) && !p.r.clicked(kTurn));
        }
    }
    // R1 first, L1 a few frames later: still the turn, and the reload that R1 waits for is dropped.
    {
        Pad p;
        p.frame(16);
        p.down = {kR1};
        p.frame(16);
        SH_CHECK(!p.r.clicked(kReload) && !p.r.held(kReload));  // waiting for the window
        p.frame(16);
        p.frame(16);
        p.down = {kL1, kR1};
        p.frame(16);
        SH_CHECK(p.r.clicked(kTurn));
        for (int t = 0; t < 400; t += 16) {
            p.frame(16);
            SH_CHECK(!p.r.clicked(kReload));
        }
    }
    // R1 alone: its reload fires once the window has passed without L1, then is held.
    {
        Pad p;
        p.frame(16);
        p.down = {kR1};
        p.frame(16);
        int fired = 0;
        std::uint32_t at = 0;
        for (int t = 0; t < 400; t += 16) {
            p.frame(16);
            if (p.r.clicked(kReload)) {
                ++fired;
                at = p.now;
            }
        }
        SH_CHECK(fired == 1 && at - 32 >= kChordWindowMs && at - 32 < kChordWindowMs + 16);
        SH_CHECK(p.r.held(kReload) && !p.r.clicked(kTurn));
    }
    // A tap of R1 shorter than the window still reloads.
    {
        Pad p;
        p.frame(16);
        p.down = {kR1};
        p.frame(16);
        p.down.clear();
        p.frame(16);
        int fired = 0;
        for (int t = 0; t < 300; t += 16) {
            p.frame(16);
            fired += p.r.clicked(kReload) ? 1 : 0;
        }
        SH_CHECK(fired == 1);
    }
    // Too far apart: L1 held for long, then R1 is R1's own reload, at once, and no turn.
    {
        Pad p;
        p.frame(16);
        p.down = {kL1};
        p.frame(16);
        p.frame(500);
        p.down = {kL1, kR1};
        p.frame(16);
        SH_CHECK(!p.r.clicked(kTurn) && p.r.clicked(kReload));
    }
}

SH_TEST(pad_chord_buttons_outside_any_combination_fire_at_once) {
    Pad p;
    p.frame(16);
    p.down = {kX};
    p.frame(16);
    SH_CHECK(p.r.clicked(kLight) && p.r.held(kLight));
    // The old single-button quick turn: L1 alone turns at once, as before.
    Pad old("rightshoulder", "leftshoulder");
    old.frame(16);
    old.down = {kL1};
    old.frame(16);
    SH_CHECK(old.r.clicked(kTurn));
    old.down = {kL1, kR1};
    old.frame(16);
    SH_CHECK(old.r.clicked(kReload) && !old.r.clicked(kTurn));
}

SH_TEST(pad_chord_reset_forgets_held_buttons) {
    Pad p;
    p.frame(16);
    p.down = {kL1, kR1};
    p.frame(16);
    SH_CHECK(p.r.held(kTurn));
    p.r.reset();
    SH_CHECK(!p.r.held(kTurn));
    // Held through the reset, the buttons are a new press: the combination fires again.
    p.frame(16);
    SH_CHECK(p.r.clicked(kTurn));
}
