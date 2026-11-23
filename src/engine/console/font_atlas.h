// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// A glyph atlas of a plain monospace UI font (never the game's own font): a TrueType file is rasterised with
// stb_truetype into one 8-bit coverage bitmap. The bitmap also holds a solid white block, so panels and
// highlights are drawn from the same texture as the text (one draw call for the whole console).
//
// Font choice: the user's `console_font` path, else a system monospace font
// (Consolas on Windows, Menlo on macOS, DejaVu Sans Mono on Linux), else the bundled JetBrains Mono
// (third_party/fonts/jetbrains-mono, SIL OFL 1.1, copied next to the exe as fonts/JetBrainsMonoNL-Regular.ttf).

#pragma once

#include "formats/common/status.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace sh::console {

struct Glyph {
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;  // texture rectangle (0..1)
    float x = 0, y = 0;                    // top-left of the bitmap relative to the cell's top-left, pixels
    float w = 0, h = 0;                    // bitmap size, pixels (0 for blanks)
};

class FontAtlas {
public:
    // Rasterises ASCII, Latin-1, Latin Extended-A, Greek, Cyrillic and common box/arrow symbols at
    // `pixel_height` (the em height; the cell height is a little larger). Fails on a bad font.
    static Result<FontAtlas> build(std::span<const std::uint8_t> ttf, float pixel_height);

    float cell_width() const { return cell_w_; }     // monospace advance, pixels
    float cell_height() const { return cell_h_; }    // line height, pixels
    float ascent() const { return ascent_; }         // baseline offset from the cell top
    float pixel_height() const { return pixel_height_; }

    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    const std::vector<std::uint8_t>& pixels() const { return pixels_; }  // R8, width*height, top row first
    // The solid block (u0..u1, v0..v1 inside it all read full coverage).
    Glyph solid() const { return solid_; }

    // The glyph for a code point; a missing one gives '?' (and a blank for control characters).
    const Glyph& glyph(char32_t cp) const;
    bool has(char32_t cp) const;

private:
    struct Range { char32_t first; char32_t last; std::size_t offset; };
    std::vector<Range> ranges_;
    std::vector<Glyph> glyphs_;
    Glyph solid_;
    std::vector<std::uint8_t> pixels_;
    std::uint32_t width_ = 0, height_ = 0;
    float cell_w_ = 0, cell_h_ = 0, ascent_ = 0, pixel_height_ = 0;
    friend struct FontAtlasBuilder;
};

// Candidate font files in preference order: `configured` (when non-empty), the OS monospace fonts, then
// `bundled`. Only existing files are returned.
std::vector<std::filesystem::path> font_candidates(const std::filesystem::path& configured,
                                                   const std::filesystem::path& bundled);

// Reads the first loadable candidate. Fails when none exists/parses (the caller then has no console font and
// should log it; the console stays usable headless).
Result<std::vector<std::uint8_t>> load_first_font(const std::vector<std::filesystem::path>& candidates,
                                                  std::filesystem::path* used = nullptr);

// Decodes one UTF-8 code point at `pos` (advancing it); invalid bytes give U+FFFD and advance by one.
char32_t next_code_point(const std::string& s, std::size_t& pos);

}  // namespace sh::console
