// SPDX-License-Identifier: GPL-3.0-or-later
// The lake of the ending (map6_s02): ripples spread over a
// 16 x 16 grid of the lake surface in eight rings each, drawn as glowing quads from texture pages that are
// smoothed copies of LHEFFECT.TIM (each page is the previous one blurred once), over a dimmed copy of the screen.
// Also the ending's 2D sprites and the title sliding in.
//
//  The ripple words live where the
// game keeps them (a work buffer of the map), so they survive like the original's; the game gives the view,
// the projection, the screen copy, the video memory upload and the sprites' table (LakeHost, EndingHost).

#pragma once

#include <array>
#include <cstdint>

#include "engine/effects/effect_prims.h"
#include "render/draw3d/draw3d.h"

namespace sh::effects {

constexpr int kLakeRings = 8;
constexpr int kLakeCells = 16;
// The ripple words: kLakeRings x 256 ring phases (0 = no ripple), then 256 spread speeds.
constexpr int kLakeRippleWords = kLakeRings * 256 + 256;
// A texture page of the lake: 256 x 256 texels of 4 bits, 32 words a row.
constexpr int kLakePageWords = 256 * 32;

class LakeHost {
public:
    virtual ~LakeHost() = default;
    virtual std::int32_t rand16() = 0;
    virtual std::int32_t delta_time() = 0;
    // Sets the lake's fixed view and returns the distance to the projection plane.
    virtual std::int32_t begin_view() = 0;
    virtual void project3(const Vec3s v[3], ScreenPoint out[3]) = 0;
    virtual ScreenPoint project(const Vec3s& v) = 0;
    // The dimmed copy of the displayed frame the glow is drawn over.
    virtual void screen_glow() = 0;
    virtual void emit(const draw3d::Polygon& polygon, std::int32_t bucket) = 0;
    // Puts a page of texels (kLakePageWords words) in video memory at texture page `page`.
    virtual void upload_page(std::int32_t page, const std::uint32_t* words) = 0;
};

class LakeSurface {
public:
    // The pages after `first`: page i + 1 is page i blurred, ping-ponging between the two buffers (the first one
    // holds page `first`'s texels).
    static void build_pages(LakeHost& host, const std::array<std::uint8_t, 8>& pages, int first,
                            std::uint32_t* buffers[2]);
    // One blur of a 4-bit page: each texel becomes the rounded eighth of its eight neighbours.
    static void blur(const std::uint32_t* in, std::uint32_t* out);
    // Random first ripples and the base spread speed.
    static void seed(LakeHost& host, std::uint32_t* ripples);
    static void draw(LakeHost& host, std::uint32_t* ripples, const std::array<std::uint8_t, 8>& pages);
};

// The ending's 2D pieces: the sprite table entries are a screen box (x, y, width, height) and its texel origin.
struct EndingSprite {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::uint8_t w = 0;
    std::uint8_t h = 0;
    std::uint8_t u = 0;
    std::uint8_t v = 0;
};

struct EndingLook {
    std::int32_t jitter_x = 0;  // the shake the script sets
    std::int32_t jitter_y = 0;
    std::uint16_t palette = 0;  // the ending image's palette word
    std::int32_t sprite_page = 0;   // texture page column of the sprites
    std::int32_t title_page = 0;    // texture page column of the title
};

class UfoEnding {
public:
    // Sprite `index` of the table (index 4 shakes and comes from the next page). Returns the polygon for bucket 0.
    static draw3d::Polygon sprite(const EndingSprite& s, int index, const EndingLook& look);
    // The two halves of the title sliding in at `progress` (Q12, 0..1), for bucket 0.
    static std::array<draw3d::Polygon, 2> slide_in(std::int16_t progress, const EndingLook& look);
};

}  // namespace sh::effects
