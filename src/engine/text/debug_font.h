// SPDX-License-Identifier: GPL-3.0-or-later
// The 8x8 debug font layout: one record per input byte (the sink reserves a packet
// slot for EVERY byte, spaces included, and links one texture-page primitive at the
// end), following the original's fall-through advance.

#pragma once

#include <cstdint>

namespace sh::text {

struct DebugGlyph {
    bool present = false;        // false: the byte advances the cursor but draws nothing
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t index = 0;      // (char)toupper(byte) - base_char; may be negative
};

class DebugGlyphSink {
public:
    virtual ~DebugGlyphSink() = default;
    virtual void glyph(const DebugGlyph& record) = 0;
};

struct DebugFontSpec {
    std::int32_t base_char = '*';  // atlas index base
    std::int32_t advance = 8;
    std::int32_t line_height = 8;
};

struct DebugDrawResult {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

// Lays out a NUL-terminated string from the cursor, returning the end cursor.
DebugDrawResult draw_debug(DebugGlyphSink& sink, const DebugFontSpec& spec, const char* text,
                           std::int32_t cursor_x, std::int32_t cursor_y, std::int32_t line_start_x);

}  // namespace sh::text
