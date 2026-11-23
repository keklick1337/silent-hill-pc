// SPDX-License-Identifier: GPL-3.0-or-later
//
// The full-screen fade to black or white and the cutscene letterbox bars. The engine computes the fade level, the bar shades and the 2D commands; the host gives the
// game's state (the packed fade status, the custom fade step, frame time) and places the commands in the frame.
#pragma once

#include "render/draw2d/draw2d.h"

#include <cstdint>

namespace sh::screens {

// The packed fade status the game keeps: the state in bits 0..2, white in bit 3.
enum class ScreenFadeState : std::int32_t {
    Reset = 0,
    None = 1,
    FadeOutStart = 2,
    FadeOutSteps = 3,
    ResetTimestep = 4,
    FadeOutComplete = 5,
    FadeInStart = 6,
    FadeInSteps = 7,
};
constexpr std::int32_t kFadeWhiteBit = 1 << 3;

class ScreenFadeHost {
public:
    virtual ~ScreenFadeHost() = default;
    virtual std::int32_t status() = 0;
    virtual void set_status(std::int32_t status) = 0;
    virtual std::int32_t timestep() = 0;  // Q12 per second; 0 lets the file queue pace the fade out
    virtual void set_timestep(std::int32_t step) = 0;
    virtual int file_queue_length() = 0;
    virtual std::int32_t frame_time() = 0;  // raw Q12 seconds of the frame
    virtual int buffer() = 0;               // double-buffer index of the frame being built
    // The fade's commands, in submission order, at the fade's slot of the 2D layer (drawn in reverse order).
    virtual void submit(const draw2d::Command& command) = 0;
};

class ScreenFade {
public:
    void update(ScreenFadeHost& host);
    // How far the picture is shown (Q12, 1.0 fully visible), as of the start of the last update.
    std::int32_t visible_progress() const { return 4096 - prev_progress_; }
    std::int32_t progress() const { return progress_; }

private:
    void set_level(int buf, std::int32_t progress);
    void set_page(int buf, std::int32_t status);

    std::int32_t progress_ = 0;
    std::int32_t prev_progress_ = 0;
    // Per buffer: the tile's grey and page persist until the next draw for that buffer writes them; the tile
    // starts red, as the original's packet memory did.
    draw2d::Rgb color_[2] = {{255, 0, 0}, {255, 0, 0}};
    std::uint16_t page_[2] = {64, 64};
};

// The frame time a fade step uses: 0 (a blocking loader loop) counts as 1/60 s so the fade still moves, and a
// load hitch is capped at 1/30 s so a fade is never skipped in one frame.
std::int32_t fade_frame_time(std::int32_t raw);

// --- Cutscene letterbox bars ---
enum class BorderState : std::int32_t {
    Reset = 0,
    None = 1,
    FadeInStart = 18,
    FadingIn = 19,
    ForceShow = 20,
    Shown = 21,
    FadeOutStart = 22,
    FadingOut = 23,
};

class CutsceneBordersHost {
public:
    virtual ~CutsceneBordersHost() = default;
    virtual std::int32_t state() = 0;
    virtual void set_state(std::int32_t state) = 0;
    virtual bool music_paused() = 0;
    virtual std::int32_t frame_time() = 0;  // Q12 seconds
    virtual int buffer() = 0;
    virtual bool cutscene_active() = 0;
    virtual void clear_cutscene_active() = 0;
    virtual int screen_height() = 0;
    // Half width of the visible frame in screen units and the outer / inner bar edge distance from the centre.
    virtual int half_width() = 0;
    virtual int bar_outer() = 0;
    virtual int bar_inner() = 0;
    virtual void set_event_camera_rate(std::int32_t rate) = 0;
    virtual void set_projection(int screen_distance) = 0;
    virtual void submit(const draw2d::Command& command) = 0;
};

class CutsceneBorders {
public:
    void update(CutsceneBordersHost& host);
    std::int32_t shade() const { return shade_; }

private:
    void set_shade(int buf, std::int32_t shade);

    std::int32_t shade_ = 0;
    // [0]/[1] top bar per buffer, [2]/[3] bottom bar; outer and inner edge shades persist per buffer.
    std::uint8_t bar_shade_[4][2] = {};
};

}  // namespace sh::screens
