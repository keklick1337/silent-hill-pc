// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/text/debug_font.h"

#include <cctype>

namespace sh::text {

DebugDrawResult draw_debug(DebugGlyphSink& sink, const DebugFontSpec& spec, const char* text,
                           std::int32_t cursor_x, std::int32_t cursor_y, std::int32_t line_start_x) {
    DebugDrawResult out;
    std::int32_t pos_x = cursor_x;
    std::int32_t pos_y = cursor_y;

    for (const char* p = text; *p != '\0'; ++p) {
        const std::int32_t code = static_cast<std::uint8_t>(*p);
        DebugGlyph record;
        record.x = pos_x;
        record.y = pos_y;
        switch (code) {
            default:
                // toupper and the signed-char cast are the original's; the index may
                // go negative and the sink packs it with the original's arithmetic.
                record.present = true;
                record.index = static_cast<std::int8_t>(std::toupper(code)) - spec.base_char;
                pos_x += spec.advance;  // the original falls through to the advance
                break;

            case 95:  // '_'
            case 32:  // ' '
            case 9:   // '\t'
            case 11:  // '\v'
                pos_x += spec.advance;
                break;

            case 126:  // '~'
                pos_x -= spec.advance;
                break;

            case 10:  // '\n'
                pos_x = line_start_x;
                pos_y += spec.line_height;
                break;
        }
        sink.glyph(record);
    }

    out.x = pos_x;
    out.y = pos_y;
    return out;
}

}  // namespace sh::text
