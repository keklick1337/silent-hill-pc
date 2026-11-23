// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The frozen-frame latch and the pause return (engine/gameflow/freeze_latch.h): a recorded run tick by tick, a
// long seeded run against its recorded digest, and the rules in words.

#include <string>

#include "framework/golden.h"
#include "framework/sh_test.h"

#include "engine/gameflow/freeze_latch.h"

using namespace sh::gameflow;

namespace {

constexpr const char* kTable = "gameflow/freeze_latch.txt";

class RecordingHost final : public FreezeHost {
public:
    bool return_pending = false;
    std::string log;

    bool pause_return_pending() const override { return return_pending; }
    bool pause_return_take() override {
        call("return_take");
        const bool v = return_pending;
        return_pending = false;
        return v;
    }
    void pause_state_enter() override { call("state_enter"); }
    void pause_reset() override { call("reset"); }
    void pause_away_update() override { call("away_update"); }

private:
    void call(const char* name) {
        if (!log.empty()) log += ',';
        log += name;
    }
};

FreezeInputs in_game() {
    FreezeInputs in;
    in.in_game = true;
    return in;
}

}  // namespace

SH_TEST(freeze_rules_run_as_recorded) {
    const auto rows = sh::test::golden_rows(kTable, "tick");
    SH_CHECK(rows.size() == 2500);
    FreezeRules rules;
    RecordingHost host;
    int differences = 0;
    for (const auto& r : rows) {
        FreezeInputs in;
        in.pickup_active = r.flag(0);
        in.in_game = r.flag(1);
        in.at_or_below_load_screen = r.flag(2);
        in.load_screen_or_menu = r.flag(3);
        in.sys_gameplay = r.flag(4);
        in.pause_flag = r.flag(5);
        FreezeLatch latch;
        latch.present_last_frame = r.flag(6);
        latch.release_pending = r.flag(7);
        host.return_pending = r.flag(8);
        host.log.clear();
        rules.update(in, latch, host);
        if (latch.present_last_frame != r.flag(9) || latch.release_pending != r.flag(10) ||
            host.return_pending != r.flag(11) || host.log != r.str(12)) {
            if (++differences <= 3) {
                sh::test::golden_mismatch(kTable, r,
                                          std::to_string(latch.present_last_frame) + " " +
                                              std::to_string(latch.release_pending) + " " +
                                              std::to_string(host.return_pending) + " " + host.log);
            }
        }
    }
    SH_CHECK(differences == 0);
}

SH_TEST(freeze_rules_seeded_run_digest) {
    const auto rows = sh::test::golden_rows(kTable, "digest");
    SH_CHECK(rows.size() == 1);
    for (const auto& r : rows) {
        sh::test::Sequence q(static_cast<std::uint64_t>(r.num(0)));
        sh::test::Digest d;
        FreezeRules rules;
        RecordingHost host;
        FreezeLatch latch;
        for (std::int64_t i = 0; i < r.num(1); ++i) {
            FreezeInputs in;
            in.pickup_active = q.one_in(3);
            const int state = q.below(4);  // in game / load screen or main menu / under the load screen / another
            in.in_game = state == 0;
            in.at_or_below_load_screen = state == 1 || state == 2;
            in.load_screen_or_menu = state == 1;
            in.sys_gameplay = q.below(2) != 0;
            in.pause_flag = q.below(2) != 0;
            if (q.one_in(5)) latch.present_last_frame = q.below(2) != 0;
            if (q.one_in(5)) latch.release_pending = q.below(2) != 0;
            if (q.one_in(7)) host.return_pending = q.below(2) != 0;
            host.log.clear();
            rules.update(in, latch, host);
            d.add(latch.present_last_frame ? 1 : 0);
            d.add(latch.release_pending ? 1 : 0);
            d.add(host.return_pending ? 1 : 0);
            d.add(host.log);
        }
        SH_CHECK(sh::test::golden_digest_is(kTable, r, 2, d));
    }
}

// A frozen frame whose release was asked for stays up while the paused handler keeps the world draw gated off,
// and drops on the first tick that draws.
SH_TEST(freeze_rules_release_waits_for_a_drawn_tick) {
    FreezeRules rules;
    RecordingHost host;
    FreezeLatch latch;
    latch.present_last_frame = true;
    latch.release_pending = true;
    FreezeInputs in = in_game();
    in.pause_flag = true;
    rules.update(in, latch, host);
    SH_CHECK(latch.present_last_frame && latch.release_pending);
    in.pause_flag = false;
    rules.update(in, latch, host);
    SH_CHECK(!latch.present_last_frame && !latch.release_pending);
}

// The frozen frame behind a pickup goes away on the tick the pickup does, not before.
SH_TEST(freeze_rules_pickup_end_drops_the_frame) {
    FreezeRules rules;
    RecordingHost host;
    FreezeLatch latch;
    FreezeInputs in = in_game();
    in.pickup_active = true;
    latch.present_last_frame = true;
    rules.update(in, latch, host);
    rules.update(in, latch, host);
    SH_CHECK(latch.present_last_frame);
    in.pickup_active = false;
    rules.update(in, latch, host);
    SH_CHECK(!latch.present_last_frame);
    latch.present_last_frame = true;  // no pickup ended on this tick: untouched
    rules.update(in, latch, host);
    SH_CHECK(latch.present_last_frame);
}

// The title side never shows a frozen frame, and a pending release is forgotten there and outside the game.
SH_TEST(freeze_rules_title_side_clears_the_latch) {
    FreezeRules rules;
    RecordingHost host;
    FreezeLatch latch;
    latch.present_last_frame = true;
    FreezeInputs in;
    in.at_or_below_load_screen = true;
    rules.update(in, latch, host);
    SH_CHECK(!latch.present_last_frame && !latch.release_pending);

    latch.present_last_frame = true;
    latch.release_pending = true;
    rules.update(FreezeInputs{}, latch, host);  // some other state: the release fires
    SH_CHECK(!latch.present_last_frame && !latch.release_pending);
}

// A return to the pause is taken only in plain gameplay, where it enters the paused state; anywhere outside the
// game it only drops the frozen frame and stays pending. The pause is reset on the load screen and the main menu,
// and its away timer runs every tick.
SH_TEST(freeze_rules_pause_return) {
    FreezeRules rules;
    RecordingHost host;
    FreezeLatch latch;
    host.return_pending = true;
    FreezeInputs in = in_game();
    rules.update(in, latch, host);  // in game, but not plain gameplay
    SH_CHECK(host.log == "away_update" && host.return_pending);
    host.log.clear();
    in.sys_gameplay = true;
    rules.update(in, latch, host);
    SH_CHECK(host.log == "return_take,state_enter,away_update" && !host.return_pending);
    host.log.clear();
    rules.update(in, latch, host);  // nothing pending: taken, not entered
    SH_CHECK(host.log == "return_take,away_update");

    host.log.clear();
    host.return_pending = true;
    latch.present_last_frame = true;
    FreezeInputs menu;
    menu.load_screen_or_menu = true;
    rules.update(menu, latch, host);
    SH_CHECK(host.log == "reset,away_update" && host.return_pending && !latch.present_last_frame);
}
