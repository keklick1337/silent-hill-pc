// SPDX-License-Identifier: GPL-3.0-or-later
// The staff roll's 24-pixel font: the glyph advance table, the two text drawers (flat
// sprites and text on a tilted plane in perspective) and the draw records they emit.
// The module decides WHAT to draw; the game bridge turns the records into primitives
// and supplies the texture placement (page, palette) it alone knows.

#pragma once

#include <array>
#include <cstdint>

namespace sh::screens {

// Byte codes of the roll's text encoding (the markup in staff_roll_markup.h writes them).
namespace roll_code {
constexpr std::uint8_t kNewline = 0x0A;
constexpr std::uint8_t kLineDown = 0x0B;      // next line, same column
constexpr std::uint8_t kHome = 0x0C;          // back to where the string started
constexpr std::uint8_t kReturn = 0x0D;        // back to the margin
constexpr std::uint8_t kColorFirst = 0x01;    // 0x01..0x07 pick a color of the table
constexpr std::uint8_t kColorLast = 0x07;
constexpr std::uint8_t kGlyphFirst = 0x21;    // atlas glyphs 0x21..0x84
constexpr std::uint8_t kGlyphEnd = 0x85;
constexpr std::uint8_t kLogoFirst = 0xA0;     // logo glyphs, first set 0xA0..0xAF
constexpr std::uint8_t kLogoEnd = 0xB0;
constexpr std::uint8_t kLogo2First = 0xB8;    // logo glyphs, second set 0xB8..0xBE
constexpr std::uint8_t kLogo2End = 0xBF;
constexpr std::uint8_t kSpanFirst = 0xD0;     // 0xD0..0xD7 open a measured span n
constexpr std::uint8_t kSpanEnd = 0xD8;       // 0xD8..0xDF close span n
constexpr std::uint8_t kSpanCloseOffset = 8;
// A measured span n sets the advances of four codes: +half (0xE0+n), +full (0xE8+n),
// -half (0xF0+n), -full (0xF8+n).
constexpr std::uint8_t kForwardHalf = 0xE0;
constexpr std::uint8_t kForwardFull = 0xE8;
constexpr std::uint8_t kBackHalf = 0xF0;
constexpr std::uint8_t kBackFull = 0xF8;
}  // namespace roll_code

// Font data of the roll: advances per byte code (the measured-span codes are rewritten
// while drawing, as in the original, so the table is shared state of both drawers),
// the atlas glyph each logo code stands for in the perspective drawer, the color table.
struct RollFont {
    std::array<std::int16_t, 256> advance{};
    std::array<std::uint8_t, 16> logo_glyphs{};   // codes 0xA0..0xAF
    std::array<std::uint8_t, 8> logo_glyphs_2{};  // codes 0xB8..0xBF
    std::array<std::uint32_t, 7> colors{};        // codes 1..7, r | g << 8 | b << 16
};

// One record of a roll string.
//  - Glyph: a 24x24 atlas cell copied to (x[0], y[0]); uv[0] = u | v << 8 of the cell.
//  - LogoGlyph: a 16x16 logo cell at (x[0], y[0]); uv[0] as above.
//  - Quad: a textured quad, corners top-left, top-right, bottom-left, bottom-right in
//    x/y and uv (uv words u | v << 8, as the original packed them).
//  - TexturePage: the end of a flat string (the original closes it with a page change).
//  - Bar: an untextured black rectangle at (x[0], y[0]) of x[1] by y[1] pixels.
struct RollPrim {
    enum class Kind : std::uint8_t { Glyph, LogoGlyph, Quad, TexturePage, Bar };
    Kind kind = Kind::Glyph;
    std::int32_t x[4] = {};
    std::int32_t y[4] = {};
    std::int32_t uv[4] = {};
    std::uint32_t rgb = 0;
    bool semi_transparent = false;
};

class RollSink {
public:
    virtual ~RollSink() = default;
    virtual void prim(const RollPrim& record) = 0;
};

// Flat roll text: sprites at a cursor, one string per call; the cursor stays where the
// string ended.
class FlatRollText {
public:
    explicit FlatRollText(RollFont& font) : font_(font) {}

    void set_position(std::int32_t x, std::int32_t y);
    void set_color(std::uint32_t rgb) { rgb_ = rgb; }
    void set_semi_transparent(bool on) { semi_ = on; }
    void set_line_height(std::int32_t height) { line_height_ = height; }

    // Draws a NUL-terminated byte string; a null pointer draws nothing.
    void draw(const std::uint8_t* text, RollSink& sink);

    std::int32_t x() const { return x_; }
    std::int32_t y() const { return y_; }

private:
    RollFont& font_;
    std::int32_t x_ = 0;
    std::int32_t y_ = 0;
    std::int32_t margin_ = 0;
    std::int32_t line_height_ = 24;
    std::uint32_t rgb_ = 0xFFFFFF;
    bool semi_ = false;
};

// The plane the perspective text lies on, in Q.12: a text point (x, y) maps to
// X = origin_x + scale * x * column_x, Y = origin_y + scale * y * row_y,
// Z = origin_z + scale * y * row_z, projected with focal / Z.
struct RollPlane {
    std::int32_t focal = 240;
    std::int32_t scale = 4096;
    std::int32_t origin_x = 0;
    std::int32_t origin_y = 0;
    std::int32_t origin_z = 0xF0000;
    std::int32_t column_x = 4096;
    std::int32_t row_y = 0;
    std::int32_t row_z = -4096;
};

// Perspective roll text. The projected cursor (left edge on the glyph's top and bottom
// rows, the per-pixel step of both, the two rows' screen y) is cached and advanced
// incrementally exactly as the original did, with its fixed-point truncations.
class PerspectiveRollText {
public:
    explicit PerspectiveRollText(RollFont& font) : font_(font) {}

    // A new plane; the cursor goes to (0, 0), color white, opaque.
    void set_plane(const RollPlane& plane);
    void set_position(std::int32_t x, std::int32_t y);
    void draw(const std::uint8_t* text, RollSink& sink);

    const RollPlane& plane() const { return plane_; }

private:
    struct Projection {
        std::int32_t top_x = 0;     // screen x of the cursor, glyph top row (Q.12)
        std::int32_t bottom_x = 0;  // same, glyph bottom row
        std::int32_t top_dx = 4096; // screen x step per text pixel, top row
        std::int32_t bottom_dx = 4551;
        std::int32_t top_y = 0;     // screen y of the glyph rows (Q.12)
        std::int32_t bottom_y = 0;
    };
    Projection project(std::int32_t x, std::int32_t y) const;

    RollFont& font_;
    RollPlane plane_;
    Projection p_;
    std::int32_t x_ = 0;
    std::int32_t y_ = 0;
    std::int32_t margin_ = 0;
    std::int32_t line_height_ = 24;
    std::uint32_t rgb_ = 0xFFFFFF;
    bool semi_ = false;
};

}  // namespace sh::screens
