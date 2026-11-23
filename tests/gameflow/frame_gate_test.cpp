// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include "engine/gameflow/frame_gate.h"

using sh::gameflow::FrameGate;
using sh::gameflow::FrameGateInputs;
using sh::gameflow::FrameGateParams;

namespace {

FrameGateInputs world_frame() {
    FrameGateInputs in;
    in.in_game = true;
    in.world_drawn = true;
    in.now_ms = 1000;
    return in;
}

}  // namespace

SH_TEST(frame_gate_world_frames_are_wide) {
    FrameGate gate(FrameGateParams::builtin());
    const auto out = gate.update(world_frame());
    SH_CHECK(out.wide);
    SH_CHECK(out.ui_wide);
    SH_CHECK(out.frame_is_3d_world);
    SH_CHECK(!out.bg2d_held);
}

SH_TEST(frame_gate_narrows_through_the_hysteresis) {
    FrameGate gate(FrameGateParams::builtin());
    SH_CHECK(gate.update(world_frame()).wide);

    // A menu frame: not in-game, world not drawn -- drops immediately.
    FrameGateInputs menu = world_frame();
    menu.in_game = false;
    menu.world_drawn = false;
    SH_CHECK(!gate.update(menu).wide);

    // Back wide, then a state-transition fade (opaque tile keeps the framing).
    SH_CHECK(gate.update(world_frame()).wide);
    FrameGateInputs fading = world_frame();
    fading.in_game = false;
    fading.fade_holds_framing = true;
    SH_CHECK(gate.update(fading).wide);

    // The fade ends with the world still drawn: 200 ms grace, then narrow.
    FrameGateInputs grace = world_frame();
    grace.in_game = false;
    grace.now_ms = 5000;
    SH_CHECK(gate.update(grace).wide);  // grace starts now
    grace.now_ms = 5199;
    SH_CHECK(gate.update(grace).wide);  // still inside
    grace.now_ms = 5200;
    SH_CHECK(!gate.update(grace).wide); // deadline reached
}

SH_TEST(frame_gate_bg2d_hold_and_decay) {
    FrameGate gate(FrameGateParams::builtin());

    // A fullscreen 2D background drew this frame (counter 2): held, narrow.
    FrameGateInputs bg = world_frame();
    bg.bg2d_active = 2;
    auto out = gate.update(bg);
    SH_CHECK(out.bg2d_active == 1);  // decremented
    SH_CHECK(out.bg2d_held);
    SH_CHECK(!out.wide);

    // The counter runs out but the 300 ms hold bridges the TIM-swap gap
    // (the 2D screen still covers the world, so the world-drawn release
    // does not fire).
    FrameGateInputs gap = world_frame();
    gap.bg2d_active = 0;
    gap.in_game = false;
    gap.world_drawn = false;
    gap.now_ms = bg.now_ms + 299;
    out = gate.update(gap);
    SH_CHECK(out.bg2d_held);
    SH_CHECK(!out.wide);

    // The first world frame after the 2D screen ends the hold at once.
    FrameGateInputs back = world_frame();
    back.now_ms = bg.now_ms + 100;  // still inside the hold window
    out = gate.update(back);
    SH_CHECK(!out.bg2d_held);
    SH_CHECK(out.wide);
}

SH_TEST(frame_gate_paper_map_and_cutscene_flags) {
    FrameGate gate(FrameGateParams::builtin());

    // The paper-map tick narrows (a 2D screen), cutscene or not.
    FrameGateInputs paper = world_frame();
    paper.skip_framebuffer_store = true;
    SH_CHECK(!gate.update(paper).wide);

    // ...but the same flag during a cutscene must not narrow one.
    FrameGate gate2(FrameGateParams::builtin());
    FrameGateInputs ending = world_frame();
    ending.skip_framebuffer_store = true;
    ending.cutscene_live = true;
    SH_CHECK(gate2.update(ending).wide);

    // The map overlay narrows; a presented freeze-frame counts as the world
    // for the hold's release but not for width.
    FrameGate gate3(FrameGateParams::builtin());
    FrameGateInputs mapf = world_frame();
    mapf.map_screen_active = true;
    SH_CHECK(!gate3.update(mapf).wide);
}

SH_TEST(frame_gate_params_parse) {
    const auto builtin = FrameGateParams::builtin();
    SH_CHECK(builtin.bg2d_hold_ms == 300);
    SH_CHECK(builtin.narrow_hysteresis_ms == 200);

    auto ok = FrameGateParams::parse("{\"bg2d_hold_ms\": 500, \"narrow_hysteresis_ms\": 100}");
    SH_CHECK(ok);
    SH_CHECK(ok.value().bg2d_hold_ms == 500);
    SH_CHECK(ok.value().narrow_hysteresis_ms == 100);

    // Unknown or out-of-range keys are errors; an empty object keeps built-ins.
    auto empty = FrameGateParams::parse("{}");
    SH_CHECK(empty);
    SH_CHECK(empty.value().bg2d_hold_ms == 300);
    SH_CHECK(!FrameGateParams::parse("{\"bg2d_hold_ms\": -1}"));
    SH_CHECK(!FrameGateParams::parse("{\"narrow_hysteresis_ms\": true}"));
    SH_CHECK(!FrameGateParams::parse("not json"));
}
