// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the menus' draw layer (nm_draw.h, nm_draw.cpp) needs from the running game: the screen
// mode, the frame clocks, the menu pad, the options screen's selection widgets and the 2D layers of the frame.
// Implemented on the game's records.
#pragma once

#include "render/draw2d/draw2d.h"

#include <cstdint>

namespace sh::ui {

struct NmPad {
    bool up = false, down = false, left = false, right = false;
    bool confirm = false, cancel = false;
};

class NmDrawHost {
public:
    virtual ~NmDrawHost() = default;

    // The 448-line title screen (the menus' coordinates double vertically there).
    virtual bool interlaced() = 0;
    // Game ticks, and the frame counter the game's screens animate their arrows by.
    virtual std::int32_t tick() = 0;
    virtual std::int32_t frame_counter() = 0;
    // Pad edges of this frame without the bits in `exclude` (the mouse's bound buttons).
    virtual NmPad pad(std::uint32_t exclude) = 0;

    // The tall glyphs of the 448-line screen.
    virtual void set_hi_res_glyphs(bool on) = 0;
    // The 2D layer kept apart from the world geometry filter (-1: none).
    virtual void set_ui_layer(std::int32_t layer) = 0;

    // A command into 2D layer `layer` of the menu list.
    virtual void submit(std::int32_t layer, const draw2d::Command& command) = 0;
    // The default texture page for the layer's untextured translucent commands.
    virtual void reset_texture_page(std::int32_t layer) = 0;
    // A 3D preview item drawn between two layers.
    virtual void link_scene(std::int32_t layer, std::uint32_t item) = 0;

    // The options screen's selection widgets (centre-relative points).
    virtual void widget_arrow(const draw2d::Point tri[3]) = 0;
    virtual void widget_underline(draw2d::Point a, draw2d::Point b, bool shadow) = 0;
    virtual void widget_bullet(const draw2d::Point quad[4], bool border, bool dim) = 0;
};

NmDrawHost& nm_draw_host();

}  // namespace sh::ui
