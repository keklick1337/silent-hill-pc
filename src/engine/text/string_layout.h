// SPDX-License-Identifier: GPL-3.0-or-later
// Screen-space string layout over the cell font: the markup of the game's draw strings
// (remapped literals, spaces, control bytes, colors and the per-character kerning of the
// mapper), the clip window and the optional per-string scale.

#pragma once

#include "engine/text/text_types.h"

namespace sh::text {

struct StringDrawQuery {
    const char* text = nullptr;   // NUL-terminated; walked raw, like the original
    std::int32_t length = 0;      // glyph budget; control bytes do not consume it
    std::int32_t cursor_x = 0;
    std::int32_t cursor_y = 0;
    std::int32_t line_start_x = 0;  // x a newline returns to
    std::int32_t color_id = 7;
    bool hi_res = false;
    std::int32_t scale = kUnityScale;  // about the string's start position
    std::int32_t clip_x0 = 0;          // string-space window [x0, x1); inactive when
    std::int32_t clip_x1 = 0;          // x1 <= x0
};

struct StringDrawResult {
    bool terminated = false;      // stopped at a NUL byte (budget still left)
    std::int16_t cursor_x = 0;    // end position, truncated to 16 bits
    std::int16_t cursor_y = 0;
    std::int32_t color_id = 0;    // after any in-string color bytes
};

StringDrawResult draw_string(GlyphSink& sink, CharSource& chars, const FontAtlas& font,
                             const ColorTable& colors, const StringDrawQuery& query);

}  // namespace sh::text
