// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/screens/background.h"

namespace sh::screens {
namespace {

draw2d::Point point(std::int32_t x, std::int32_t y) {
    return {static_cast<std::int16_t>(x), static_cast<std::int16_t>(y)};
}

draw2d::TexCoord texel(std::uint32_t word) {
    return {static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8)};
}

std::uint32_t palette_word(const BackgroundImage& image) {
    return static_cast<std::uint32_t>((image.clut_y << 6) | ((image.clut_x >> 4) & 0x3F));
}

// The hardware's texture page word: depth, blend, the 64-pixel column of x and the 256-pixel row of y.
std::uint16_t page_word(int depth, int blend, std::int32_t x, std::int32_t y) {
    return static_cast<std::uint16_t>(((depth & 3) << 7) | ((blend & 3) << 5) | ((y & 0x100) >> 4) |
                                      ((x & 0x3FF) >> 6) | ((y & 0x200) << 2));
}

// The grey the game stores as one 16-bit c + c * 256 (carrying into blue's byte only through c itself).
draw2d::Rgb grey(std::uint32_t c) {
    const auto rg = static_cast<std::uint16_t>(c + (c << 8));
    return {static_cast<std::uint8_t>(rg), static_cast<std::uint8_t>(rg >> 8), static_cast<std::uint8_t>(c)};
}

// One column of a 320x240 picture: the right edge at x1 (the left edge and colour are the caller's), texels from
// (0, v) over `width` texels and 240 rows. The texel words carry as the game's 32-bit adds did.
draw2d::Polygon column_quad(const BackgroundImage& image, std::int32_t x1, std::int32_t width, std::uint16_t page) {
    const std::uint32_t w0 = (static_cast<std::uint32_t>(image.v) << 8) + (palette_word(image) << 16);
    const std::uint32_t w1 = (static_cast<std::uint32_t>(image.v) << 8) + width + (static_cast<std::uint32_t>(page) << 16);
    const auto w2 = static_cast<std::uint16_t>((image.v + 239) << 8);
    const auto w3 = static_cast<std::uint16_t>(((image.v + 239) << 8) + width);
    draw2d::Polygon poly;
    poly.count = 4;
    poly.textured = true;
    poly.xy[1] = point(x1, -120);
    poly.xy[3] = point(x1, 120);
    poly.uv[0] = texel(w0);
    poly.uv[1] = texel(w1);
    poly.uv[2] = texel(w2);
    poly.uv[3] = texel(w3);
    poly.palette = draw2d::Palette::from_word(static_cast<std::uint16_t>(w0 >> 16));
    poly.page = draw2d::TexturePage::from_word(static_cast<std::uint16_t>(w1 >> 16));
    return poly;
}

// Column k (0..2) of a picture: 128, 128 and 64 texels wide, from page column + k.
draw2d::Polygon picture_column(const BackgroundImage& image, int k, int blend, std::uint32_t color) {
    const std::int32_t left = 128 * k;
    draw2d::Polygon poly = column_quad(image, left + (k != 2 ? -32 : -96), k != 2 ? 128 : 64,
                                       page_word(image.depth, blend, (image.column + k) << 6, (image.column << 4) & 0x100));
    poly.xy[0] = point(-160 + left, -120);
    poly.xy[2] = point(-160 + left, 120);
    poly.color[0] = grey(color);
    return poly;
}

}  // namespace

std::vector<draw2d::Command> background_tiles(const BackgroundImage& image, int screen_w, int screen_h,
                                              std::uint8_t gamma) {
    std::vector<draw2d::Command> out;
    const unsigned depth = image.depth;
    const std::int32_t base_y = -120 << (screen_h >> 8);
    const std::uint32_t clut = palette_word(image);
    const std::uint32_t v = image.v;
    const std::int32_t u_screen = image.u << (2 - depth);  // the picture's u in screen pixels
    for (int y = 0; (screen_h >> 8) >= y; ++y) {
        for (int x = 0; ((screen_w - 1) >> (8 - depth)) >= x; ++x) {
            // Never past the picture's own width: the one-pixel shift left below would otherwise pull a column of
            // whatever lies beyond it into the right edge.
            std::int32_t tile_w = (screen_w + u_screen) - (x << 8);
            if (tile_w <= 0) continue;
            if (tile_w > 256) tile_w = 256;

            draw2d::Sprite sprite;
            sprite.color = {gamma, gamma, gamma};
            sprite.w = static_cast<std::uint16_t>(tile_w);
            if (y == 0) {
                const std::uint32_t word = (v << 8) + (clut << 16);
                sprite.h = static_cast<std::uint16_t>(256 - v);
                sprite.uv = texel(word);
                sprite.palette = draw2d::Palette::from_word(static_cast<std::uint16_t>(word >> 16));
            } else {
                sprite.h = 256;
                sprite.palette = draw2d::Palette::from_word(static_cast<std::uint16_t>(clut));
            }
            // One pixel left of the original placement hides the renderer's clip of the first column.
            sprite.at = point(((x << 8) - (screen_w >> 1)) - u_screen - 1,
                              static_cast<std::int32_t>(static_cast<std::uint32_t>(base_y) + (256 - v) * y));
            out.emplace_back(sprite);

            draw2d::DrawMode mode;
            mode.page = draw2d::TexturePage::from_word(
                page_word(static_cast<int>(depth), 0, (image.column + (x << depth)) << 6,
                          ((image.column << 4) & 0x100) + (y << 8)));
            mode.dither = true;
            out.emplace_back(mode);
        }
    }
    return out;
}

std::array<draw2d::Polygon, 3> background_columns(const BackgroundImage& image, std::uint8_t gamma) {
    std::array<draw2d::Polygon, 3> out;
    for (int k = 0; k < 3; ++k) out[k] = picture_column(image, k, 0, gamma);
    return out;
}

std::array<draw2d::Polygon, 9> background_transition(const BackgroundImage& from, const BackgroundImage& to,
                                                     std::int16_t alpha) {
    std::array<draw2d::Polygon, 9> out;
    const auto faded = static_cast<std::uint32_t>(static_cast<std::int32_t>((static_cast<std::int64_t>(alpha) * 128) >> 12));
    for (int pass = 0; pass < 3; ++pass) {
        const BackgroundImage& image = pass > 0 ? from : to;
        for (int k = 0; k < 3; ++k) {
            draw2d::Polygon poly = picture_column(image, k, pass + 1, pass < 2 ? faded : 128);
            poly.shade.translucent = pass < 2;
            out[pass * 3 + k] = poly;
        }
    }
    return out;
}

std::vector<draw2d::Command> motion_blur_tiles(int screen_w, bool interlaced, int buffer, BlurDecay& decay) {
    std::vector<draw2d::Command> out;
    for (int pass = 0; pass == 0 || (interlaced && pass == 1); ++pass) {
        for (int k = 0; k < 3; ++k) {
            std::int32_t v, y, page_y;
            if (interlaced) {
                v = pass == 0 ? 0x20 : 0;
                y = pass == 0 ? -224 : 0;
                page_y = pass << 8;
            } else {
                y = -112;
                v = (buffer == 0) << 5;
                page_y = buffer << 8;
            }
            // Only the framebuffer's own width: a tile past it would sample texture memory beside it.
            std::int32_t tile_w = screen_w - (k << 8);
            if (tile_w <= 0) continue;
            if (tile_w > 256) tile_w = 256;

            draw2d::Sprite sprite;
            const std::uint8_t level = decay.level();
            sprite.color = {level, level, level};
            const auto word = static_cast<std::uint32_t>(v << 8);
            sprite.w = static_cast<std::uint16_t>(tile_w);
            sprite.h = 224;
            sprite.uv = texel(word);
            sprite.palette = draw2d::Palette::from_word(static_cast<std::uint16_t>(word >> 16));
            sprite.at = point((-screen_w / 2) + (k << 8), y);
            out.emplace_back(sprite);

            draw2d::DrawMode mode;
            mode.page = draw2d::TexturePage::from_word(page_word(2, 0, k << 8, page_y));
            mode.dither = true;
            out.emplace_back(mode);
        }
    }
    return out;
}

}  // namespace sh::screens
