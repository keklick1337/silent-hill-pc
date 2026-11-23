// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The per-frame presentation gate of the main loop (
// section 18):
// whether this frame is drawn wide (Hor+) or 4:3, the 2D-background hold
// that bridges fullscreen-2D screens' TIM-swap gaps, and the wide->narrow
// hysteresis that rides state-transition fades out in wall-clock time.
// Pure decisions over a snapshot of frame signals; the host feeds the signals
// (including the wall clock) and writes the answers to its renderer globals.
//
// Numbers are gamedata/config/frame_gate.json; a mod replaces the file whole.

#pragma once

#include <cstdint>
#include <string_view>

#include "formats/common/status.h"

namespace sh::gameflow {

struct FrameGateParams {
    std::uint32_t bg2d_hold_ms = 300;          // a 2D background drawn this frame keeps 4:3 this long
    std::uint32_t narrow_hysteresis_ms = 200;  // wide->narrow grace after a world frame

    static FrameGateParams builtin();
    static Result<FrameGateParams> parse(std::string_view json);
};

struct FrameGateInputs {
    bool in_game = false;              // the 3D-world game state
    bool skip_framebuffer_store = false;  // the paper-map "2D screen up" signal
    bool cutscene_live = false;        // cutscene flag or letterbox bars
    bool map_screen_active = false;    // the paper-map overlay is displayed
    bool world_drawn = false;          // the world was submitted this tick
    bool present_last_frame = false;   // a captured frame is being presented
    bool fade_holds_framing = false;   // the fade tile is fully opaque: keep the framing it started under
    std::int32_t bg2d_active = 0;      // the game's 2D-background counter (re-armed by its 2D drawers)
    std::uint32_t now_ms = 0;          // the wall clock, one read per frame
};

struct FrameGateOutputs {
    std::int32_t bg2d_active = 0;   // the decremented counter (write back)
    bool bg2d_held = false;         // a fullscreen 2D background ran within the hold
    bool wide = false;              // the framing this frame renders with (Hor+)
    bool ui_wide = false;           // the framing the 2D UI pass wants (same value, latched before the world asserts)
    bool frame_is_3d_world = false; // the renderer's "is this the 3D world" class: same expression as `wide`'s input
};

class FrameGate {
public:
    explicit FrameGate(FrameGateParams params) : params_(params) {}

    FrameGateOutputs update(const FrameGateInputs& in);

private:
    FrameGateParams params_;
    bool wide_ = false;                    // the framing latch (drops only through a branch that says so)
    std::uint32_t bg2d_hold_until_ms_ = 0; // 2D-background hold deadline
    std::uint32_t narrow_off_at_ms_ = 0;   // wide->narrow deadline; 1 means "already dropped"
};

}  // namespace sh::gameflow
