// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/screens/cursor_shapes.h"

#include <algorithm>

namespace sh::screens {
namespace {

std::int32_t mul_q12(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 12);
}

draw2d::TexCoord texel(std::uint32_t word) {
    return {static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8)};
}

draw2d::Point point(std::int32_t x, std::int32_t y) {
    return {static_cast<std::int16_t>(x), static_cast<std::int16_t>(y)};
}

}  // namespace

draw2d::Polygon puzzle_cursor_quad(const PuzzleCursor& c) {
    // The texel words are summed, not or-ed, as the game's 32-bit stores did: a u past 255 carries into v, a v
    // past 255 into the palette / page half.
    const std::uint32_t palette = (static_cast<std::uint32_t>(c.clut_y) << 6) | ((c.clut_x >> 4) & 0x3F);
    const std::uint32_t page = static_cast<std::uint32_t>(c.page & 0xF) | (static_cast<std::uint32_t>((c.page >> 4) & 1) << 4);
    const std::uint32_t w0 = static_cast<std::uint32_t>(c.u + (c.v << 8)) + (palette << 16);
    const std::uint32_t w1 = static_cast<std::uint32_t>((c.u + c.w) + (c.v << 8)) + (page << 16);
    const auto w2 = static_cast<std::uint16_t>(c.u + ((c.v + c.h) << 8));
    const auto w3 = static_cast<std::uint16_t>((c.u + c.w) + ((c.v + c.h) << 8));
    const auto rg = static_cast<std::uint16_t>(c.shade + (c.shade << 8));

    draw2d::Polygon poly;
    poly.count = 4;
    poly.textured = true;
    poly.xy[0] = point(c.x - c.half_w, c.y - c.half_h);
    poly.xy[1] = point(c.x + c.half_w, c.y - c.half_h);
    poly.xy[2] = point(c.x - c.half_w, c.y + c.half_h);
    poly.xy[3] = point(c.x + c.half_w, c.y + c.half_h);
    poly.uv[0] = texel(w0);
    poly.uv[1] = texel(w1);
    poly.uv[2] = texel(w2);
    poly.uv[3] = texel(w3);
    poly.palette = draw2d::Palette::from_word(static_cast<std::uint16_t>(w0 >> 16));
    poly.page = draw2d::TexturePage::from_word(static_cast<std::uint16_t>(w1 >> 16));
    poly.color[0] = {static_cast<std::uint8_t>(rg), static_cast<std::uint8_t>(rg >> 8),
                     static_cast<std::uint8_t>(c.shade)};
    return poly;
}

std::array<draw2d::Line, kOutlineLines> outline_box_lines(std::int16_t t, OutlineBox from, OutlineBox to) {
    std::array<draw2d::Line, kOutlineLines> out{};
    int n = 0;
    for (int ring = 0; ring < 5; ++ring) {
        std::int16_t blend;
        if (t > 0) {
            const std::int32_t delayed = (t - (ring * 2048) / 5) << 12;
            blend = static_cast<std::int16_t>(std::min<std::int32_t>(std::max<std::int32_t>(delayed / 2048, 0), 4096));
        } else {
            blend = static_cast<std::int16_t>(-t);
        }
        const std::int32_t a = blend;
        const std::int32_t rest = 4096 - a;

        // The game keeps the left/right edges unsigned and the top/bottom ones signed, each truncated to 16 bits
        // at every step.
        std::uint16_t xs[2];
        std::int16_t ys[2];
        xs[0] = static_cast<std::uint16_t>(mul_q12(from.x, rest) + mul_q12(to.x, a));
        ys[0] = static_cast<std::int16_t>(mul_q12(from.y, rest) + mul_q12(to.y, a));
        auto edge = static_cast<std::int16_t>(xs[0] + mul_q12(from.w, rest));
        xs[1] = static_cast<std::uint16_t>(mul_q12(to.w, a) + edge);
        edge = static_cast<std::int16_t>(ys[0] + mul_q12(from.h, rest));
        ys[1] = static_cast<std::int16_t>(mul_q12(to.h, a) + edge);

        const auto blue = static_cast<std::uint8_t>(0x80 - ((ring << 6) / 5));
        const std::int32_t x0 = xs[0], x1 = xs[1], y0 = ys[0], y1 = ys[1];
        // The box's top-right and bottom-left halves, each doubled one pixel outwards.
        const std::int32_t corners[4][3][2] = {
            {{x0, y0}, {x1, y0}, {x1, y1}},
            {{x0, y0 - 1}, {x1, y0 - 1}, {x1, y1 + 1}},
            {{x1, y1}, {x0, y1}, {x0, y0}},
            {{x1, y1 + 1}, {x0, y1 + 1}, {x0, y0 - 1}},
        };
        for (const auto& c : corners) {
            draw2d::Line& line = out[n++];
            line.count = 3;
            line.color[0] = {0, 0, blue};
            for (int v = 0; v < 3; ++v) line.xy[v] = point(c[v][0], c[v][1]);
        }
    }
    return out;
}

}  // namespace sh::screens
