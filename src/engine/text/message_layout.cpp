// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/text/message_layout.h"

#include "engine/text/glyph_emit.h"

namespace sh::text {
namespace {

// Line-0 y of the message box for a position id, as the anchors prescribe for the
// rendered line count. Ids beyond the table (and Keep entries) keep the previous y.
std::int16_t box_anchor_y(const MessageSpec& spec, const FontAtlas& font, std::uint8_t position_idx,
                          std::int32_t line_count, std::int32_t fallback_y) {
    if (position_idx >= 5) {
        return static_cast<std::int16_t>(fallback_y);
    }
    std::int32_t y = fallback_y;
    switch (spec.anchors.formula[position_idx]) {
        case MessageBoxAnchors::Formula::Fixed:
            y = spec.anchors.y_base[position_idx];
            break;
        case MessageBoxAnchors::Formula::RisePerLine:
            y = spec.anchors.y_base[position_idx] - ((line_count - 1) * font.line_height);
            break;
        case MessageBoxAnchors::Formula::Grid8:
            y = ((spec.anchors.grid_lines - line_count) * 8) + spec.anchors.y_base[position_idx];
            break;
        case MessageBoxAnchors::Formula::Keep:
            break;
    }
    return static_cast<std::int16_t>(y);
}

// The glyphs a line holds from `p` to its end (a line break, a page or message end), read
// the way the draw loop reads them.
std::int32_t count_line_glyphs(const char* p, CharSource& chars) {
    std::int32_t count = 0;
    for (;;) {
        const std::uint8_t c = static_cast<std::uint8_t>(*p);
        if (c == 0) {
            return count;
        }
        if (c == '_' || c == ' ' || c == '\t' || c == '\n') {
            ++p;
            continue;
        }
        if (c == '~') {
            const std::uint8_t tag = static_cast<std::uint8_t>(p[1]);
            if (tag == 0 || tag == 'N' || tag == 'E' || tag == 'S' || tag == 'P') {
                return count;
            }
            if (tag == 'J') {
                // The timer's text runs to the first blank.
                p += 2;
                while (*p != '\0' && *p != ' ' && *p != '\t') {
                    ++p;
                }
            } else {
                p += p[2] != '\0' ? 2 : 1;
            }
            if (*p != '\0') {
                ++p;
            }
            continue;
        }
        std::uint32_t code = c;
        p += (code >= 0x80 && chars.wide_text()) ? read_wide_char(p, code) : 1;
        ++count;
    }
}

}  // namespace

MessageMeasureResult MessageLayout::measure(CharSource& chars, const FontAtlas& font,
                                            const MessageSpec& spec,
                                            const MessageMeasureQuery& query) const {
    MessageMeasureResult out;
    out.line_count = 1;
    out.audio_load_block = 0;
    out.position_idx = query.position_idx;
    out.hi_res = query.hi_res;
    for (int i = 0; i < 12; ++i) {
        out.widths[i] = query.widths[i];
    }
    // Only the slots under the line cap are cleared; the ones above keep whatever the
    // live table held, exactly as the original's clear loop.
    for (std::int32_t i = spec.line_cap - 1; i >= 0; --i) {
        out.widths[i] = 0;
    }

    const char* p = query.msg + page_start(query.msg);

    for (std::int32_t line = 0; line < spec.line_cap;) {
        const std::int32_t char_code = static_cast<std::uint8_t>(*p);
        switch (char_code) {
            case '\t':
            case '\n':
            case ' ':
                ++p;
                break;

            case '_':
                ++p;
                out.widths[out.line_count - 1] += font.space_advance;
                break;

            case '~': {
                const std::int32_t msg_code = static_cast<std::uint8_t>(*++p);
                std::int32_t pos_idx = static_cast<std::uint8_t>(*++p) - '0';
                switch (msg_code) {
                    case 'C':  // color: measured width does not change
                    case 'S':  // select prompt
                    case 'T':  // inset
                        break;

                    case 'N':
                        ++line;
                        ++out.line_count;
                        break;

                    case 'E':
                    case 'P':
                        line = spec.line_cap;
                        break;

                    case 'L':
                        out.position_idx = pos_idx;
                        break;

                    case 'J':
                        if (pos_idx == 2) {
                            out.audio_load_block = 3;
                        }
                        // Walk to the white space after the timer text; the raw walk
                        // (reading whatever bytes are there) is the original's.
                        while (pos_idx != ' ' && pos_idx != '\t') {
                            pos_idx = static_cast<std::uint8_t>(*++p);
                        }
                        break;

                    case 'H':
                        out.hi_res = true;
                        break;

                    default:
                        break;
                }
                ++p;
                break;
            }

            case 0:
                line = spec.line_cap;
                break;

            default: {
                std::int32_t mapped = char_code;
                if (mapped == '!') {
                    mapped = '\\';
                } else if (mapped == '&') {
                    mapped = '^';
                }

                std::uint32_t code = static_cast<std::uint32_t>(mapped);
                int length = 1;
                if (code >= 0x80 && chars.wide_text()) {
                    length = read_wide_char(p, code);
                }
                GlyphEmit emits[2];
                const int emit_count = chars.map_char(code, emits);
                for (int i = 0; i < emit_count; ++i) {
                    out.widths[out.line_count - 1] += emits[i].advance;
                }
                p += length;
                break;
            }
        }
    }

    // A page that ended on the line bound counted the ~N opening a line it never drew;
    // clamp so the draw side never reads past the rendered lines.
    if (out.line_count > spec.line_cap) {
        out.line_count = spec.line_cap;
    }
    return out;
}

MessageDrawResult MessageLayout::draw(GlyphSink& sink, CharSource& chars, const FontAtlas& font,
                                      const ColorTable& colors, const MessageSpec& spec,
                                      const MessageDrawQuery& query) {
    MessageDrawResult out;
    out.color_id = query.color_id;

    if (query.msg != page_msg_) {
        page_msg_ = query.msg;
        page_off_ = 0;
        page_next_ = 0;
    }
    const char* base = query.msg;
    const char* p = base + page_off_;

    std::int32_t color_id = query.color_id;
    std::int32_t result = 0;
    if (page_off_ != 0) {
        color_id = page_color_id_;
        result = page_align_;
    }
    out.color_id = color_id;

    bool hi_res = query.hi_res;
    std::int32_t budget = query.length;
    bool budget_max = budget >= spec.display_all_length;

    detail::GlyphStyle style;
    style.color = colors.rgb[color_id & 7];

    // Box placement: line-0 y from the position id, lifted for the bottom boxes during
    // gameplay; line 0 centred on the widest rendered line.
    const std::uint8_t position_idx = static_cast<std::uint8_t>(query.position_idx);
    std::int16_t pos_y = box_anchor_y(spec, font, position_idx, query.line_count, query.fallback_y);
    if ((position_idx == 1 || position_idx == 3 || position_idx == 4) && !spec.cutscene_active) {
        pos_y = static_cast<std::int16_t>(pos_y - static_cast<std::int16_t>(spec.msg_v_shift));
    }

    std::int32_t longest = query.widths[0];
    for (std::int32_t i = 0; i < query.line_count; ++i) {
        if (longest < query.widths[i]) {
            longest = query.widths[i];
        }
    }

    const std::int16_t pos_x = static_cast<std::int16_t>(-(longest >> 1));
    std::int32_t line_start = pos_x;  // g_StringPositionX1
    std::int32_t glyph_x = line_start;
    std::int32_t glyph_y = pos_y;

    // Where a line starts in a right-to-left language: so that it ends at the block's right
    // edge, or at the mirrored inset.
    const bool rtl = spec.right_to_left;
    const auto rtl_line_x = [&](std::int32_t line, bool inset) {
        return (inset ? -spec.tab_x : pos_x + longest) - query.widths[line];
    };
    if (rtl) {
        glyph_x = rtl_line_x(0, false);
    }

    if (page_off_ != 0) {
        // A continuation page starts after the codes that set the alignment, so apply
        // what those handlers apply.
        if (result == spec.align_center_code) {
            glyph_x = -(query.widths[0] >> 1);
        } else if (result == spec.set_by_t_code) {
            line_start = spec.tab_x;
            glyph_x = rtl ? rtl_line_x(0, true) : spec.tab_x;
        }
    }

    // Size setup: the box grows from its first (or, for the bottom boxes, last) line,
    // shrunk until the widest line and the whole block fit the screen.
    bool has_tab = false;
    for (const char* s = p; s[0] != '\0'; ++s) {
        if (s[0] == '~' && s[1] == 'T') {
            has_tab = true;
            break;
        }
    }
    const bool inset = result == spec.set_by_t_code || has_tab;

    std::int32_t scale = spec.text_size_scale;
    const std::int32_t height = query.line_count * font.line_height;
    const bool from_bottom = position_idx == 1 || position_idx == 3 || position_idx == 4;
    std::int32_t half_width = longest >> 1;
    if (inset) {
        half_width = (longest - spec.fit_inset_base > spec.fit_inset_base)
                         ? (longest - spec.fit_inset_base)
                         : spec.fit_inset_base;
    }
    const std::int32_t scale_anchor_y = from_bottom ? (pos_y + height) : pos_y;
    if (scale <= kUnityScale || query.line_count <= 0) {
        scale = kUnityScale;
    } else {
        if (half_width > 0 && ((static_cast<std::int64_t>(half_width) * scale) >> 12) > spec.fit_half_width) {
            scale = static_cast<std::int32_t>(
                (static_cast<std::int64_t>(spec.fit_half_width) << 12) / half_width);
        }
        const std::int32_t room =
            from_bottom ? (scale_anchor_y + spec.fit_room_top) : (spec.fit_room_top - scale_anchor_y);
        if (height > 0 && ((static_cast<std::int64_t>(height) * scale) >> 12) > room) {
            scale = static_cast<std::int32_t>((static_cast<std::int64_t>(room) << 12) / height);
        }
        if (scale < kUnityScale) {
            scale = kUnityScale;
        }
    }

    // Fill in the placement fields every exit path reports.
    const auto fill_outputs = [&](bool early) {
        out.code = result;
        out.setup_x = pos_x;
        out.setup_y = pos_y;
        out.line_start_x = line_start;
        out.color_id = color_id;
        out.hi_res = hi_res;
        out.wrote_cursor = !early;
        if (!early) {
            out.cursor_x = static_cast<std::int16_t>(static_cast<std::uint16_t>(glyph_x));
            out.cursor_y = static_cast<std::int16_t>(static_cast<std::uint16_t>(glyph_y));
        }
    };
    style.scale = scale;
    style.anchor_x = 0;
    style.anchor_y = scale_anchor_y;
    style.hi_res = hi_res;

    // The rollout ran out mid-glyph: leave a continuation when the budget is at its
    // ceiling, and return without moving the cursor.
    const auto stop_at_budget = [&]() {
        if (budget_max) {
            result = break_page(base, p, color_id, result, spec.page_code);
        }
        fill_outputs(true);
        return out;
    };

    // Rolling a right-to-left line out: the glyphs the budget does not reach yet are the
    // ones at the line's left end, which is where the text ends.
    std::int32_t hidden = 0;
    const auto start_line = [&](const char* from) {
        hidden = 0;
        if (rtl && !budget_max) {
            const std::int32_t total = count_line_glyphs(from, chars);
            hidden = total > budget ? total - budget : 0;
        }
    };
    start_line(p);

    bool terminated = false;
    for (std::int32_t line = 0; line < spec.line_cap;) {
        std::uint32_t code = static_cast<std::uint8_t>(*p);
        if (code == '!') {
            code = '\\';
        } else if (code == '&') {
            code = '^';
        }

        switch (code) {
            case '_':
                glyph_x += font.space_advance;
                ++p;
                break;

            case ' ':
            case '\t':
                ++p;
                break;

            case '~': {
                const std::int32_t tag = static_cast<std::uint8_t>(*++p);
                std::int32_t arg = static_cast<std::uint8_t>(*++p) - '0';
                switch (tag) {
                    default:
                        break;

                    case 'N':
                        ++line;
                        if (result == spec.align_center_code) {
                            glyph_x = -(query.widths[line] >> 1);
                        } else if (result == spec.set_by_t_code) {
                            glyph_x = rtl ? rtl_line_x(line, true) : line_start;
                        } else {
                            glyph_x = rtl ? rtl_line_x(line, false) : -(longest >> 1);
                        }
                        glyph_y += font.line_height;
                        // The code takes the byte after its letter; the line begins past it.
                        start_line(p[0] != '\0' ? p + 1 : p);
                        break;

                    case 'J':
                        if (!query.timer_running) {
                            // "~J<d>(<seconds>)": decimal seconds to Q12, the fraction
                            // divided out after the shift (truncating per digit).
                            out.audio_load_block_written = true;
                            out.audio_load_block = arg + 1;

                            p += 2;  // past the digit and '('
                            bool after_point = false;
                            std::int32_t fraction_digits = 0;
                            std::int32_t value = 0;
                            for (; *p != ')'; ++p) {
                                if (*p == '.') {
                                    after_point = true;
                                    continue;
                                }
                                if (after_point) {
                                    ++fraction_digits;
                                }
                                value = (value * 10) + (*p - '0');
                            }
                            value <<= 12;
                            for (std::int32_t i = 0; i < fraction_digits; ++i) {
                                value /= 10;
                            }
                            out.timer_written = true;
                            out.timer_value = value;
                            ++p;  // past ')'
                        } else {
                            while (arg != ' ' && arg != '\t') {
                                arg = static_cast<std::uint8_t>(*++p);
                            }
                        }
                        break;

                    case 'M':
                        result = spec.align_center_code;
                        glyph_x = -(query.widths[line] >> 1);
                        break;

                    case 'T':
                        result = spec.set_by_t_code;
                        line_start = spec.tab_x;
                        glyph_x = rtl ? rtl_line_x(line, true) : spec.tab_x;
                        break;

                    case 'C':
                        // The color table holds eight ids; a digit beyond them keeps
                        // the current color (no shipped text uses
                        // one, the original read past the table).
                        if (arg >= 0 && arg <= 7) {
                            style.color = colors.rgb[arg];
                            color_id = arg;
                            out.color_id = arg;
                        }
                        break;

                    case 'D':
                        // Re-armed here, so the rollout is at its ceiling whatever it
                        // was on entry.
                        budget = spec.display_all_length;
                        budget_max = true;
                        hidden = 0;
                        break;

                    case 'E':
                        result = spec.end_code;
                        line = spec.line_cap;
                        terminated = true;
                        break;

                    case 'S':
                        result = arg;
                        line = spec.line_cap;
                        terminated = true;
                        break;

                    case 'P':
                        line = spec.line_cap;
                        break;

                    case 'H':
                        hi_res = true;
                        style.hi_res = true;
                        break;
                }
                ++p;
                break;
            }

            case 0:
                result = spec.page_code;
                line = spec.line_cap;
                terminated = true;
                break;

            default: {
                int length = 1;
                if (code >= 0x80 && chars.wide_text()) {
                    length = read_wide_char(p, code);
                }
                GlyphEmit emits[2];
                const int emit_count = chars.map_char(code, emits);
                if (hidden > 0) {
                    // Not rolled out yet: it keeps its place and costs no budget.
                    --hidden;
                    for (int i = 0; i < emit_count; ++i) {
                        glyph_x += emits[i].advance;
                    }
                    p += length;
                    break;
                }
                --budget;
                for (int i = 0; i < emit_count; ++i) {
                    std::int32_t page = 0, u = 0, v_top = 0, clut = 0;
                    emit_location(font, emits[i], page, u, v_top, clut);
                    const std::int32_t draw_y = glyph_y + emits[i].dy;
                    detail::emit_glyph(sink, font, style, glyph_x, draw_y, u, v_top, page, clut, 0, 0, emits[i].height);
                    glyph_x += emits[i].advance;
                }
                p += length;

                if (budget <= 0) {
                    return stop_at_budget();
                }
                break;
            }
        }
    }

    // Ending the parse on the line bound (a ~P or an over-long page) leaves no result
    // code; turn the page instead so the message can still be dismissed.
    if (!terminated) {
        result = break_page(base, p, color_id, result, spec.page_code);
    }

    fill_outputs(false);
    return out;
}

void MessageLayout::reset_pages() {
    page_msg_ = nullptr;
    page_off_ = 0;
    page_next_ = 0;
}

void MessageLayout::page_advance() {
    page_off_ = page_next_;
    page_next_ = 0;
}

std::int32_t MessageLayout::page_start(const char* msg) const {
    return (msg != nullptr && msg == page_msg_) ? page_off_ : 0;
}

std::int32_t MessageLayout::break_page(const char* base, const char* at, std::int32_t color_id,
                                       std::int32_t align, std::int32_t page_code) {
    page_next_ = static_cast<std::int32_t>(at - base);
    page_color_id_ = color_id;
    page_align_ = align;
    return page_code;
}

}  // namespace sh::text
