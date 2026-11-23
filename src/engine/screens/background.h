// SPDX-License-Identifier: GPL-3.0-or-later
//
// Full-screen background pictures: a picture in texture memory drawn
// over the whole screen as 256-wide sprite tiles or as three textured columns, a cross-fade between two pictures, and
// the loading screen's motion-blur feedback of the previous frame. Pure: the commands come back in drawing order of
// submission; the caller places them in the frame.
#pragma once

#include "render/draw2d/draw2d.h"

#include <array>
#include <cstdint>
#include <vector>

namespace sh::screens {

// Where a picture lies in texture memory (the game's image descriptor).
struct BackgroundImage {
    std::uint8_t depth = 0;   // texel depth code (0 4-bit, 1 8-bit, 2 15-bit)
    std::uint8_t column = 0;  // first texture page column; bit 4 the 256-pixel row
    std::uint8_t u = 0, v = 0;
    std::int16_t clut_x = 0, clut_y = 0;
};

// The picture as sprite tiles of up to 256 pixels across the screen (screen_w x screen_h, 448 high = two rows of
// tiles), tinted grey `gamma` (128 = as stored); each tile is a Sprite followed by its DrawMode.
std::vector<draw2d::Command> background_tiles(const BackgroundImage& image, int screen_w, int screen_h,
                                              std::uint8_t gamma);

// The picture as three columns over the 320x240 screen, grey `gamma`.
std::array<draw2d::Polygon, 3> background_columns(const BackgroundImage& image, std::uint8_t gamma);

// A cross-fade by `alpha` (Q12) from `from` to `to`: submitted as `to` added at alpha, `from` subtracted at alpha and
// `from` opaque, so drawn in reverse (each goes to the head of the list) they mix to from * (1 - alpha) + to * alpha.
std::array<draw2d::Polygon, 9> background_transition(const BackgroundImage& from, const BackgroundImage& to,
                                                     std::int16_t alpha);

// The previous frame copied back over the screen (one pass, two on an interlaced screen), each tile a Sprite and
// its DrawMode. `decay` is asked per tile whether this copy takes one level off the stored frame.
class BlurDecay {
public:
    virtual ~BlurDecay() = default;
    virtual bool decay_now() = 0;
    // The copy sprite's modulation (128 = keep): the original's 127 on a decaying frame, else 128.
    virtual std::uint8_t level() { return decay_now() ? 127 : 128; }
};
std::vector<draw2d::Command> motion_blur_tiles(int screen_w, bool interlaced, int buffer, BlurDecay& decay);

}  // namespace sh::screens
