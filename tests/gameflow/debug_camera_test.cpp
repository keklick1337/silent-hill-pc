// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The developer cameras' per-frame update (engine/gameflow/debug_camera.h): every input combination against the
// recorded table of host calls, and the rules in words.

#include <string>

#include "framework/golden.h"
#include "framework/sh_test.h"

#include "engine/gameflow/debug_camera.h"

using namespace sh::gameflow;

namespace {

constexpr const char* kTable = "gameflow/debug_camera.txt";

struct Mock : DebugCameraHost {
    bool kbd = false, allow = false, panel = false, game = false, free = false, tps = false, alt = false;
    std::string log;
    void call(const char* name) {
        if (!log.empty()) log += ',';
        log += name;
    }
    bool keyboard_ready() const override { return kbd; }
    bool dev_controls_allowed() const override { return allow; }
    bool console_or_debug_menu_open() const override { return panel; }
    bool in_game() const override { return game; }
    bool free_camera_on() const override { return free; }
    bool third_person_on() const override { return tps; }
    bool alt_camera_state_ok() const override { return alt; }
    void free_camera_fly() override { call("free_camera_fly"); }
    void free_camera_apply() override { call("free_camera_apply"); }
    void free_camera_set(bool on) override {
        call(on ? "free_camera_set:1" : "free_camera_set:0");
        free = on;
    }
    void third_person_apply() override { call("third_person_apply"); }
    void dev_keys_update() override { call("dev_keys_update"); }
};

// A game in play with the keyboard up and the developer controls allowed.
Mock playing() {
    Mock m;
    m.kbd = m.allow = m.game = m.alt = true;
    return m;
}

}  // namespace

SH_TEST(debug_camera_every_combination_as_recorded) {
    const auto rows = sh::test::golden_rows(kTable, "update");
    SH_CHECK(rows.size() == 128);
    for (const auto& r : rows) {
        Mock m;
        m.kbd = r.flag(0);
        m.allow = r.flag(1);
        m.panel = r.flag(2);
        m.game = r.flag(3);
        m.free = r.flag(4);
        m.tps = r.flag(5);
        m.alt = r.flag(6);
        debug_camera_update(m);
        const std::string calls = m.log.empty() ? "-" : m.log;
        const bool same = calls == r.str(7) && m.free == r.flag(8);
        SH_CHECK(same);
        if (!same) sh::test::golden_mismatch(kTable, r, calls + " " + std::to_string(m.free));
    }
}

// In play the developer keys are read first; the free camera then flies and is applied, and without it the third
// person camera is applied when it is on.
SH_TEST(debug_camera_in_play) {
    Mock m = playing();
    m.free = true;
    debug_camera_update(m);
    SH_CHECK(m.log == "dev_keys_update,free_camera_fly,free_camera_apply");
    m = playing();
    m.tps = true;
    debug_camera_update(m);
    SH_CHECK(m.log == "dev_keys_update,third_person_apply");
    m = playing();
    debug_camera_update(m);
    SH_CHECK(m.log == "dev_keys_update");
}

// An open console or debug menu holds the free camera where it is: applied, not flown, no keys read.
SH_TEST(debug_camera_holds_under_a_panel) {
    Mock m = playing();
    m.panel = m.free = true;
    debug_camera_update(m);
    SH_CHECK(m.log == "free_camera_apply" && m.free);
}

// Leaving the game turns the free camera off; the third person camera takes over at once when its state allows.
SH_TEST(debug_camera_free_camera_ends_outside_the_game) {
    Mock m = playing();
    m.game = false;
    m.free = m.tps = true;
    debug_camera_update(m);
    SH_CHECK(m.log == "free_camera_set:0,third_person_apply" && !m.free);
    m = playing();
    m.game = m.alt = false;
    m.free = m.tps = true;
    debug_camera_update(m);
    SH_CHECK(m.log == "free_camera_set:0");
}

// Without the keyboard nothing runs; without developer controls a free camera already on keeps flying.
SH_TEST(debug_camera_without_keyboard_or_permission) {
    Mock m = playing();
    m.kbd = false;
    m.free = m.tps = true;
    debug_camera_update(m);
    SH_CHECK(m.log.empty());
    m = playing();
    m.allow = false;
    m.free = true;
    debug_camera_update(m);
    SH_CHECK(m.log == "free_camera_fly,free_camera_apply");
    m = playing();
    m.allow = false;
    m.tps = true;
    debug_camera_update(m);
    SH_CHECK(m.log == "third_person_apply");
}
