// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/gameflow/frame_gate.h"

#include "formats/common/json.h"

namespace sh::gameflow {

FrameGateParams FrameGateParams::builtin() {
    return {};
}

Result<FrameGateParams> FrameGateParams::parse(std::string_view text) {
    const Result<json::Value> doc = json::parse(text);
    if (!doc) {
        return Result<FrameGateParams>::fail("frame_gate.json: " + doc.error());
    }
    FrameGateParams out = builtin();
    if (const json::Value* hold = doc.value().find("bg2d_hold_ms")) {
        if (!hold->is_integer() || hold->integer() < 0 || hold->integer() > 10000) {
            return Result<FrameGateParams>::fail(
                "frame_gate.json: \"bg2d_hold_ms\" must be an integer from 0 to 10000");
        }
        out.bg2d_hold_ms = static_cast<std::uint32_t>(hold->integer());
    }
    if (const json::Value* hys = doc.value().find("narrow_hysteresis_ms")) {
        if (!hys->is_integer() || hys->integer() < 0 || hys->integer() > 10000) {
            return Result<FrameGateParams>::fail(
                "frame_gate.json: \"narrow_hysteresis_ms\" must be an integer from 0 to 10000");
        }
        out.narrow_hysteresis_ms = static_cast<std::uint32_t>(hys->integer());
    }
    return Result<FrameGateParams>::ok(out);
}

FrameGateOutputs FrameGate::update(const FrameGateInputs& in) {
    FrameGateOutputs out;

    // The 2D-background counter decays unconditionally: the hold must end even
    // in states that never re-arm it. A counter > 0 means a fullscreen 2D
    // background was drawn this frame; that re-arms the wall-clock hold.
    out.bg2d_active = in.bg2d_active;
    if (out.bg2d_active > 0) {
        out.bg2d_active--;
        bg2d_hold_until_ms_ = in.now_ms + params_.bg2d_hold_ms;
    }
    out.bg2d_held = in.now_ms < bg2d_hold_until_ms_;

    // The hold exists to bridge TIM-swap gaps WHILE a 2D screen is up. On the
    // way out it must not outlive the world: the first world frame (or a
    // presented freeze-frame, which is the world too) ends it.
    if (out.bg2d_held && out.bg2d_active <= 0 && (in.world_drawn || in.present_last_frame)) {
        bg2d_hold_until_ms_ = 0;
        out.bg2d_held = false;
    }

    // Wide only on 3D-world frames: in-game, not a paper-map tick (a cutscene
    // legitimately raises the store-skip flag for its palettes and must not
    // narrow), no map overlay, no fullscreen 2D background.
    const bool want_wide = in.in_game && !(in.skip_framebuffer_store && !in.cutscene_live) &&
                           !in.map_screen_active && !out.bg2d_held;
    out.frame_is_3d_world = want_wide;

    // Instant to wide; to narrow through the hysteresis, except when the world
    // is not on screen at all (nothing to keep wide) or a stable 2D screen is
    // up, which drop at once. An opaque fade keeps whatever framing it started
    // under. The deadline latches at 1 ("already narrow") so the grace does not
    // re-run after an immediate drop. The framing is a latch: a branch that
    // says nothing keeps the previous frame's value.
    out.wide = wide_;
    if (want_wide) {
        narrow_off_at_ms_ = 0;
        wide_ = true;
    } else if (out.bg2d_held) {
        narrow_off_at_ms_ = 1;
        wide_ = false;
    } else if (in.fade_holds_framing) {
        // keep the framing the fade started under
    } else if (!in.world_drawn) {
        narrow_off_at_ms_ = 1;
        wide_ = false;
    } else {
        if (narrow_off_at_ms_ == 0) {
            narrow_off_at_ms_ = in.now_ms + params_.narrow_hysteresis_ms;
        }
        if (in.now_ms >= narrow_off_at_ms_) {
            wide_ = false;
        }
    }
    out.wide = wide_;
    out.ui_wide = out.wide;
    return out;
}

}  // namespace sh::gameflow
