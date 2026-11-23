// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/text/string_layout.h"

#include "engine/text/glyph_emit.h"

namespace sh::text {
namespace {

// False when the whole cell at pos_x lies outside [x0, x1); otherwise reports how many
// texels hang over each edge. An empty window (x1 <= x0) clips nothing.
bool clip_glyph(const FontAtlas& font, std::int32_t pos_x, std::int32_t x0, std::int32_t x1,
                std::int32_t& cut_left, std::int32_t& cut_right) {
    cut_left = 0;
    cut_right = 0;
    if (x1 <= x0) {
        return true;
    }
    if (pos_x + font.cell_width <= x0 || pos_x >= x1) {
        return false;
    }
    if (pos_x < x0) {
        cut_left = x0 - pos_x;
    }
    if (pos_x + font.cell_width > x1) {
        cut_right = pos_x + font.cell_width - x1;
    }
    return true;
}

}  // namespace

StringDrawResult draw_string(GlyphSink& sink, CharSource& chars, const FontAtlas& font,
                             const ColorTable& colors, const StringDrawQuery& query) {
    StringDrawResult out;
    out.color_id = query.color_id;

    const char* p = query.text;
    std::int32_t budget = query.length;
    std::int32_t pos_x = query.cursor_x;
    std::int32_t pos_y = query.cursor_y;
    const std::int32_t start_x = query.cursor_x;
    const std::int32_t start_y = query.cursor_y;

    detail::GlyphStyle style;
    style.color = colors.rgb[query.color_id & 7];
    style.hi_res = query.hi_res;
    style.scale = query.scale;
    style.anchor_x = start_x;
    style.anchor_y = start_y;

    const bool wide = chars.wide_text();
    while (budget > 0) {
        std::uint32_t code = static_cast<std::uint8_t>(*p);
        int extra = 0;  // bytes of a wide character past the first
        if (wide && code >= 0x80) {
            extra = read_wide_char(p, code) - 1;
        }

        // The font encodes '!' and '&' in the cells of '\\' and '^'.
        if (code == '!') {
            code = '\\';
        } else if (code == '&') {
            code = '^';
        }

        if (code == '_') {
            pos_x += font.space_advance;
        } else if (code == '\v') {
            pos_x += font.wide_space_advance;
        } else if (code == 0x01) {
            --pos_x;
        } else if (code >= static_cast<std::uint32_t>(font.glyph_byte_min)) {
            --budget;
            GlyphEmit emits[2];
            const int emit_count = chars.map_char(code, emits);
            for (int i = 0; i < emit_count; ++i) {
                std::int32_t page = 0, u = 0, v_top = 0, clut = 0;
                emit_location(font, emits[i], page, u, v_top, clut);
                const std::int32_t draw_y = pos_y + emits[i].dy;

                std::int32_t cut_left = 0;
                std::int32_t cut_right = 0;
                if (!clip_glyph(font, pos_x, query.clip_x0, query.clip_x1, cut_left, cut_right)) {
                    pos_x += emits[i].advance;
                    continue;
                }
                detail::emit_glyph(sink, font, style, pos_x, draw_y, u, v_top, page, clut, cut_left, cut_right,
                                   emits[i].height);
                pos_x += emits[i].advance;
            }
            p += extra;
        } else if (code == '\n') {
            pos_x = query.line_start_x;
            pos_y += font.line_height;
        } else if (code >= 2 && code < 8) {
            style.color = colors.rgb[code];
            out.color_id = static_cast<std::int32_t>(code);
        } else if (code == 0) {
            out.terminated = true;
            break;
        }

        ++p;
    }

    out.cursor_x = static_cast<std::int16_t>(static_cast<std::uint16_t>(pos_x));
    out.cursor_y = static_cast<std::int16_t>(static_cast<std::uint16_t>(pos_y));
    return out;
}

}  // namespace sh::text
