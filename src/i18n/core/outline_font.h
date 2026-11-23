// SPDX-License-Identifier: GPL-3.0-or-later
// A TrueType/OpenType face loaded for drawing, and the game-font look given to its glyphs: a character missing from
// the 12x16 cell font is drawn from an outline font into one or two cells of the same height, shaded top to bottom
// like the game's letters and with their drop shadow, at several raster pixels per game pixel.
#pragma once

#include "i18n/core/font_file.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace sh::i18n {

class OutlineFont {
public:
    // Loads the whole file of the face; null when it cannot be read or parsed.
    static std::unique_ptr<OutlineFont> load(const FontFace& face);
    static std::unique_ptr<OutlineFont> from_bytes(std::vector<std::uint8_t> bytes, std::uint32_t face_offset = 0);
    ~OutlineFont();

    bool has(char32_t cp) const;

    // Font units, y up.
    struct Box {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        bool empty = true;
    };
    Box ink_box(char32_t cp) const;
    int advance(char32_t cp) const;
    int units_per_em() const;
    // Height of a capital letter: the face's own figure, else the ink of 'H', else 0.7 em.
    int cap_height() const;

    // Adds the glyph's coverage (0..255) to `out` (width x height, rows top down), the pen at (origin_x,
    // baseline_y) pixels, `scale_x`/`scale_y` pixels per font unit.
    void draw(char32_t cp, float scale_x, float scale_y, float origin_x, float baseline_y, std::uint8_t* out, int width,
              int height) const;

private:
    struct Impl;
    explicit OutlineFont(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// The cell a glyph is drawn into, in game pixels, and the look of the game font it copies.
struct CellStyle {
    int cell_width = 12;
    int cell_height = 16;
    int scale = 4;        // raster pixels per game pixel
    int baseline = 13;    // rows from the cell top to the line the letters stand on
    int cap_height = 11;  // rows a capital letter is tall
    int row_offset = 0;   // rows of a taller cell above the game font's row 0 (row_color starts there)
    std::array<std::array<std::uint8_t, 3>, 16> row_color{};  // ink colour of each cell row, top down
    std::array<std::uint8_t, 3> shadow_color{};
    int max_cells = 2;    // a glyph wider than this many cells is squeezed into them (East Asian wide
                          // characters into one)
};

// What fits a face into the cell for one script: the size its glyphs are drawn at and the row of their baseline.
// Tall scripts (Arabic) are scaled down and moved so that their highest and lowest letters stay inside the cell.
struct FaceFit {
    float scale = 0.0f;     // game pixels per font unit
    float baseline = 13.0f; // rows from the cell top
};

// The group of scripts a character is fitted with (their letters share proportions).
enum class ScriptGroup : std::uint8_t { General, Arabic, Hebrew, Wide };
ScriptGroup script_group(char32_t cp);

FaceFit fit_face(const OutlineFont& font, ScriptGroup group, const CellStyle& style);

struct CellGlyph {
    int advance = 0;                 // game pixels the cursor moves
    int cells = 0;                   // 1 or 2, side by side; 0 = nothing to draw (a space)
    bool mark = false;               // a nonspacing mark: its cell is laid over the character before it
    std::vector<std::uint8_t> rgba;  // cells * cell_width * scale by cell_height * scale, opaque ink and shadow
};

CellGlyph render_cell_glyph(const OutlineFont& font, char32_t cp, const FaceFit& fit, const CellStyle& style);

}  // namespace sh::i18n
