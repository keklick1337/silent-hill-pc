// SPDX-License-Identifier: GPL-3.0-or-later
// Shared types of the text layout: the glyph record the engine reports, the sink that
// receives it, and the font/character services the caller provides. Everything the
// game knows (atlas geometry, colors, the glyphs of a character) comes in through these
// interfaces; the engine itself only implements the markup and the fixed-point layout
// arithmetic.

#pragma once

#include <cstdint>

namespace sh::text {

// Q.12 unity: one fixed-point whole (4096) in the scale factors.
constexpr std::int32_t kUnityScale = 4096;

// One laid-out glyph, ready to become a primitive.
//  - Sprite: a texel copy of the cell at (x[0], y[0]), w x h from the cell size, with
//    left/right edge cuts (the sink trims the width by cut_left + cut_right).
//  - Quad: a textured quad with corners TL, BL, TR, BR in x[]/y[] (already scaled when
//    a scale was active), sampling texels u..u+cell_width, v..v+cell_height-1, with
//    left/right cuts on the edge columns (the sink adjusts the left/right corners).
struct GlyphDraw {
    bool sprite = true;        // false: a quad
    bool hi_res = false;       // hi-resolution mode was active at emission (the sink
                               // picks its packet cursor by this, so it is per record)
    std::int32_t x[4] = {};    // sprite: x[0]; quad: TL, BL, TR, BR
    std::int32_t y[4] = {};
    std::int32_t u = 0;        // atlas cell top-left texel
    std::int32_t v = 0;
    std::int32_t cell_width = 12;
    std::int32_t cell_height = 16;
    std::int32_t page = 0;     // texture page value
    std::int32_t clut = 0;     // packed CLUT id
    std::int32_t cut_left = 0;
    std::int32_t cut_right = 0;
    std::uint32_t color = 0;   // r | g << 8 | b << 16
};

class GlyphSink {
public:
    virtual ~GlyphSink() = default;
    virtual void glyph(const GlyphDraw& record) = 0;
};

// One glyph a character byte maps to (a base letter, or a combining mark above it).
struct GlyphEmit {
    std::int32_t cell = 0;      // atlas cell index
    std::int32_t dy = 0;        // vertical offset of this emission
    std::int32_t advance = 0;   // cursor advance after this emission
    std::int32_t height = 0;    // texel rows of this emission's cell; 0: the font's cell_height
    // A glyph of another texture than the font atlas (the game font keeps the disc's cells in the disc's
    // texture and the rest in its own page): when set, page/u/v/clut are used and `cell` is not.
    bool located = false;
    std::int32_t page = 0;
    std::int32_t u = 0;
    std::int32_t v = 0;
    std::int32_t clut = 0;
};

// The character services the game provides. map_char maps one (already remapped) byte or
// code point to 0..2 emissions: a letter, a letter under its mark, or the two halves of a
// glyph wider than a cell (the first then advances by a cell width).
class CharSource {
public:
    virtual ~CharSource() = default;
    // With wide_text(), bytes >= 0x80 are read as UTF-8 (a stray byte as its Latin-1 character) and map_char gets
    // the codepoint; a whole sequence is one character for the length budget. Off: one byte, one character.
    virtual bool wide_text() { return false; }
    virtual int map_char(std::uint32_t code, GlyphEmit out[2]) = 0;
};

// Geometry of the active cell font. All values are the game's runtime font layout.
struct FontAtlas {
    std::int32_t columns = 21;             // atlas cells per row
    std::int32_t cell_width = 12;
    std::int32_t cell_height = 16;
    std::int32_t line_height = 16;         // rows from one line of text to the next
    std::int32_t v_base = 0;               // atlas v of grid row 0
    std::int32_t rows_per_page = 1;        // grid rows per texture page
    std::int32_t tpage_base = 0;           // texture page of grid row 0
    std::int32_t packed_clut = 0;
    std::int32_t hi_res_bottom = 30;       // hi-res quad bottom offset from 2*y
    std::int32_t glyph_byte_min = 39;      // bytes at or above this go to the mapper
    std::int32_t space_advance = 6;
    std::int32_t wide_space_advance = 10;
};

// One character of a wide text at p (see CharSource::wide_text): its codepoint in `code`, the bytes it takes as
// the result. A valid UTF-8 sequence (2..4 bytes, not overlong) is one codepoint; any other byte is itself.
inline int read_wide_char(const char* p, std::uint32_t& code) {
    const auto b = [p](int i) { return static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[i])); };
    const std::uint32_t c = b(0);
    code = c;
    if (c < 0xC2 || c > 0xF4) {
        return 1;
    }
    const int n = c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    std::uint32_t cp = c & (n == 2 ? 0x1Fu : n == 3 ? 0x0Fu : 0x07u);
    for (int i = 1; i < n; ++i) {
        if ((b(i) & 0xC0) != 0x80) {
            return 1;  // a NUL or any non-continuation byte ends the try: Latin-1
        }
        cp = (cp << 6) | (b(i) & 0x3F);
    }
    if ((n == 3 && cp < 0x800) || (n == 4 && (cp < 0x10000 || cp > 0x10FFFF)) || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return 1;
    }
    code = cp;
    return n;
}

// The texel position of an emission: its own when located, else its cell in the font atlas.
inline void emit_location(const FontAtlas& font, const GlyphEmit& e, std::int32_t& page, std::int32_t& u,
                          std::int32_t& v, std::int32_t& clut) {
    if (e.located) {
        page = e.page;
        u = e.u;
        v = e.v;
        clut = e.clut;
        return;
    }
    const std::int32_t row = e.cell / font.columns;
    v = font.v_base + ((row % font.rows_per_page) * font.cell_height);
    page = font.tpage_base + (row / font.rows_per_page);
    u = (e.cell % font.columns) * font.cell_width;
    clut = font.packed_clut;
}

// String color ids 0..7 as plain RGB. Ids outside the table are never looked up.
struct ColorTable {
    std::uint32_t rgb[8] = {};
};

}  // namespace sh::text
