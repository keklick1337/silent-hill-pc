// SPDX-License-Identifier: GPL-3.0-or-later
//
// Small 2D shapes the map events draw over the game: the free cursor of the cursor puzzles and the animated
// outline box that zooms from one rectangle to another. Pure: the
// caller places the commands in the frame.
#pragma once

#include "render/draw2d/draw2d.h"

#include <array>
#include <cstdint>

namespace sh::screens {

// A cursor sprite drawn as a quad centred on (x, y), scaled to 2 * half_w by 2 * half_h, from the atlas cell
// (u, v, w, h) of the 4-bit page `page` (bits 0..3 the 64-pixel column, bit 4 the 256-pixel row) with the
// palette at (clut_x, clut_y), tinted grey by `shade` (128 = texture as is).
struct PuzzleCursor {
    std::int32_t x = 0, y = 0;
    std::int32_t half_w = 0, half_h = 0;
    std::int32_t u = 0, v = 0, w = 0, h = 0;
    std::int32_t shade = 128;
    std::uint32_t clut_x = 0;
    std::int32_t clut_y = 0;
    std::int32_t page = 0;
};
draw2d::Polygon puzzle_cursor_quad(const PuzzleCursor& c);

// An axis-aligned box (x, y and width, height), screen units.
struct OutlineBox {
    std::int16_t x = 0, y = 0, w = 0, h = 0;
};
// Five nested rings, brightest first, each drawn as four doubled two-segment lines. `t` > 0 animates (Q12: the
// first ring reaches `to` at 0.5, each further ring 0.1 later); `t` <= 0 puts every ring at blend -t of the way
// from `from` to `to`.
constexpr int kOutlineLines = 20;
std::array<draw2d::Line, kOutlineLines> outline_box_lines(std::int16_t t, OutlineBox from, OutlineBox to);

}  // namespace sh::screens
