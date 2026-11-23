// SPDX-License-Identifier: GPL-3.0-or-later
//
// The PC's health screen cues: the green pulse of a quick heal and the
// red glow breathing in from the screen edges at low health. Both are additive 2D commands in the centre-origin
// screen space of the fade layer; the host places them in the frame.
#pragma once

#include <array>
#include <cstdint>

#include "render/draw2d/draw2d.h"

namespace sh::screens {

// Commands of one cue in submission order (the layer draws them in reverse order, so the draw mode comes last).
struct CueCommands {
    std::array<draw2d::Command, 5> list;
    int count = 0;
};

// The draw mode of both cues: additive page, dithered.
draw2d::DrawMode additive_mode();

class HealFlash {
public:
    static constexpr std::int32_t kDuration = 1433;  // Q12 seconds (0.35 s)
    static constexpr int kPeakGreen = 96;

    void start() { timer_ = kDuration; }
    std::int32_t timer() const { return timer_; }

    // One frame: counts down by `frame_time` (Q12 seconds) and, while a pulse runs, gives the full-screen green tile
    // (screen_width x 4 by screen_height x 2 from (-w, -h), wide enough for any aspect) and the draw mode. The last
    // frame of a pulse still draws, at green 0.
    CueCommands update(std::int32_t frame_time, int screen_width, int screen_height);

private:
    std::int32_t timer_ = 0;
};

struct LowHealthGlowInput {
    bool enabled = false;   // config low_health_glow
    bool gameplay = false;  // in game, gameplay sub-state
    bool cutscene = false;  // a cutscene or the frame of a full-screen picture
    std::int32_t health = 0;  // Q12
    std::int32_t frame_time = 0;  // Q12 seconds
    int half_width = 160;  // visible half extent of the screen (glow_half_width)
    int half_height = 120;
};

class LowHealthGlow {
public:
    static constexpr int kHealthMax = 20;        // glows below this many points
    static constexpr std::int32_t kPeriod = 6553;  // Q12 seconds (1.6 s) of one breath
    static constexpr int kRedMin = 36;
    static constexpr int kRedMax = 112;

    // One frame: four gouraud bands (top, bottom, left, right; red at the edge, black inward) and the draw mode, or
    // nothing. The breath advances only on frames that draw, so it holds still with the game.
    CueCommands update(const LowHealthGlowInput& in);
    std::int32_t phase() const { return phase_; }

private:
    std::int32_t phase_ = 0;
};

// Half the visible screen width in screen units for the side bands: the screen's own half width, or with Hor+ the
// window's visible extent (window aspect x pixel aspect / world horizontal scale), capped to 160..512.
int glow_half_width(int screen_width, int screen_height, bool hor_plus, int render_width, int render_height,
                    float pixel_aspect, float world_hscale);

// The game's cues (their frame slot and state are the game's records). The
// main loop runs each once per frame after the fade; a quick heal starts the flash.
HealFlash& heal_flash();
void run_heal_flash();
void run_low_health_glow();

}  // namespace sh::screens
