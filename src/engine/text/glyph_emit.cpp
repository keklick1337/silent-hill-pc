// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/text/glyph_emit.h"

namespace sh::text::detail {

void emit_glyph(GlyphSink& sink, const FontAtlas& font, const GlyphStyle& style,
                std::int32_t x, std::int32_t y, std::int32_t u, std::int32_t v,
                std::int32_t page, std::int32_t clut, std::int32_t cut_left, std::int32_t cut_right,
                std::int32_t height) {
    // A taller cell (a language's drawn glyphs) keeps the font's quad, grown downwards by its extra rows.
    const std::int32_t cell_h = height > 0 ? height : font.cell_height;
    const std::int32_t hi_res_bottom = font.hi_res_bottom + 2 * (cell_h - font.cell_height);
    GlyphDraw record;
    record.u = u;
    record.v = v;
    record.cell_width = font.cell_width;
    record.cell_height = cell_h;
    record.page = page;
    record.clut = clut;
    record.color = style.color;

    if (style.scale != kUnityScale) {
        // Scaled about the anchor: size, advance and line spacing grow together. The
        // vertical factor doubles in hi-res mode, matching the unscaled hi-res quad.
        const std::int32_t y_mul = style.hi_res ? 2 : 1;
        const std::int32_t x0 = style.anchor_x + (((x - style.anchor_x) * style.scale) >> 12);
        const std::int32_t y0 =
            (style.anchor_y * y_mul) + (((y - style.anchor_y) * y_mul * style.scale) >> 12);
        const std::int32_t w = (font.cell_width * style.scale) >> 12;
        const std::int32_t bottom = style.hi_res ? hi_res_bottom : cell_h - 1;
        const std::int32_t h = (bottom * style.scale) >> 12;
        record.sprite = false;
        record.hi_res = style.hi_res;
        record.x[0] = x0;
        record.y[0] = y0;
        record.x[1] = x0;
        record.y[1] = y0 + h;
        record.x[2] = x0 + w;
        record.y[2] = y0;
        record.x[3] = x0 + w;
        record.y[3] = y0 + h;
    } else if (style.hi_res) {
        record.sprite = false;
        record.hi_res = true;
        record.cut_left = cut_left;
        record.cut_right = cut_right;
        record.x[0] = x;
        record.y[0] = y * 2;
        record.x[1] = x;
        record.y[1] = (y * 2) + hi_res_bottom;
        record.x[2] = x + font.cell_width;
        record.y[2] = y * 2;
        record.x[3] = x + font.cell_width;
        record.y[3] = (y * 2) + hi_res_bottom;
    } else {
        record.sprite = true;
        record.hi_res = false;
        record.cut_left = cut_left;
        record.cut_right = cut_right;
        record.x[0] = x;
        record.y[0] = y;
    }
    sink.glyph(record);
}

}  // namespace sh::text::detail
