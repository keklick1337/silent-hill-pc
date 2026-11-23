// SPDX-License-Identifier: GPL-3.0-or-later
// A bitmap font file (".shbf"): one-bit glyphs of one size by Unicode code point,
// for scripts too large for the 12x16 cell sheets - gamedata/font/shinonome16.shbf is the 16-dot Japanese set.
//
//   "SHBF"  u16 version (1)  u8 width  u8 height  u32 count       (little endian)
//   count x u32   code points, ascending
//   count x height x ceil(width / 8) bytes   rows top down, the most significant bit is the left pixel
#pragma once

#include "formats/common/status.h"
#include "i18n/core/outline_font.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <vector>

namespace sh::i18n {

class BitmapFont {
public:
    static Result<BitmapFont> parse(std::vector<std::uint8_t> bytes);
    static Result<BitmapFont> load(const std::filesystem::path& file);
    // The file for the given glyphs (each height x ceil(width / 8) bytes).
    static std::vector<std::uint8_t> write(int width, int height,
                                           const std::map<char32_t, std::vector<std::uint8_t>>& glyphs);

    int width() const { return width_; }
    int height() const { return height_; }
    std::size_t size() const { return count_; }
    bool has(char32_t cp) const { return glyph(cp) != nullptr; }
    // The rows of a glyph, null when the font has no such character.
    const std::uint8_t* glyph(char32_t cp) const;
    bool pixel(const std::uint8_t* glyph, int x, int y) const;

private:
    std::vector<std::uint8_t> bytes_;
    int width_ = 0;
    int height_ = 0;
    std::size_t count_ = 0;
    std::size_t row_bytes_ = 0;
};

// The glyph as one game cell in the game font's look. A glyph wider than the cell is folded into it: columns
// that fall together are merged, so thin strokes survive (the narrow kanji of the original Japanese release).
CellGlyph render_bitmap_glyph(const BitmapFont& font, char32_t cp, const CellStyle& style);

}  // namespace sh::i18n
