// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The per-frame cheats and the d-pad movement gate (engine/gameflow/frame_cheats.h): every input combination
// against the recorded table, and the rules in words.

#include "framework/golden.h"
#include "framework/sh_test.h"

#include "engine/gameflow/frame_cheats.h"

using namespace sh::gameflow;

namespace {
constexpr const char* kTable = "gameflow/frame_cheats.txt";
}

SH_TEST(frame_cheats_every_combination_as_recorded) {
    const auto rows = sh::test::golden_rows(kTable, "cheats");
    SH_CHECK(rows.size() == 64);
    for (const auto& r : rows) {
        FrameCheatIn in;
        in.god_mode = r.flag(0);
        in.no_target = r.flag(1);
        in.fast_forward = r.flag(2);
        in.in_game = r.flag(3);
        in.game_over = r.flag(4);
        in.player_dead = r.flag(5);
        const FrameCheatOut o = frame_cheats_apply(in);
        const int skip = o.write_skip_swap ? (o.skip_swap ? 1 : 0) : -1;
        const bool same = o.fast_forward == r.flag(6) && o.refill_health == r.flag(7) && skip == r.num(8) &&
                          o.hold_no_target == r.flag(9);
        SH_CHECK(same);
        if (!same) {
            sh::test::golden_mismatch(kTable, r,
                                      std::to_string(o.fast_forward) + " " + std::to_string(o.refill_health) + " " +
                                          std::to_string(skip) + " " + std::to_string(o.hold_no_target));
        }
    }
    const auto dpad = sh::test::golden_rows(kTable, "dpad");
    SH_CHECK(dpad.size() == 16);
    for (const auto& r : dpad) {
        SH_CHECK(dpad_movement_disabled(r.flag(0), r.flag(1), r.flag(2), r.flag(3)) == r.flag(4));
    }
}

// Fast-forward does not outlive the player: death or the game over sequence turn it off, in game or not.
SH_TEST(frame_cheats_fast_forward_ends_with_the_player) {
    FrameCheatIn in;
    in.fast_forward = true;
    SH_CHECK(frame_cheats_apply(in).fast_forward);
    in.player_dead = true;
    SH_CHECK(!frame_cheats_apply(in).fast_forward);
    in.player_dead = false;
    in.game_over = true;
    SH_CHECK(!frame_cheats_apply(in).fast_forward);
    in.in_game = true;
    const FrameCheatOut o = frame_cheats_apply(in);
    SH_CHECK(!o.fast_forward && o.write_skip_swap && !o.skip_swap);
}

// Outside the game nothing is written: no health refill, no swap flag, no hold on the enemies.
SH_TEST(frame_cheats_act_only_in_game) {
    FrameCheatIn in;
    in.god_mode = in.no_target = in.fast_forward = true;
    FrameCheatOut o = frame_cheats_apply(in);
    SH_CHECK(!o.refill_health && !o.write_skip_swap && !o.hold_no_target && o.fast_forward);
    in.in_game = true;
    o = frame_cheats_apply(in);
    SH_CHECK(o.refill_health && o.write_skip_swap && o.skip_swap && o.hold_no_target);
}

// The d-pad stops moving the player only when the option is on, during plain gameplay, with the debug menu closed.
SH_TEST(frame_cheats_dpad_gate) {
    SH_CHECK(dpad_movement_disabled(true, true, true, false));
    SH_CHECK(!dpad_movement_disabled(false, true, true, false));
    SH_CHECK(!dpad_movement_disabled(true, false, true, false));
    SH_CHECK(!dpad_movement_disabled(true, true, false, false));
    SH_CHECK(!dpad_movement_disabled(true, true, true, true));
}
