// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Fixed 8x8 text for the small debug panels (the message toast, the keyframe inspector): a grid of character
// cells drawn into an RGBA image the overlay shows as one textured quad. The console itself has its own
// TrueType atlas (src/engine/console). No game or GPU types: the panel is plain memory.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sh::debug {

inline constexpr int kGlyphSize = 8;

struct Ink {
    std::uint8_t r = 255, g = 255, b = 255, a = 255;
};

// Whether the pixel (x, y) of a character's cell is set; characters outside printable ASCII are blank.
bool glyph_pixel(char character, int x, int y);

// An image of `columns` x `rows` character cells.
class TextPanel {
public:
    TextPanel(int columns, int rows)
        : columns_(columns), rows_(rows),
          pixels_(static_cast<std::size_t>(columns) * kGlyphSize * static_cast<std::size_t>(rows) * kGlyphSize * 4) {}

    int width() const { return columns_ * kGlyphSize; }
    int height() const { return rows_ * kGlyphSize; }
    const std::uint8_t* rgba() const { return pixels_.data(); }

    // Clears the image and draws the lines from the top row down in one colour; text past the panel's columns
    // and lines past its rows are left out.
    void draw(std::span<const std::string> lines, Ink ink);

private:
    int columns_;
    int rows_;
    std::vector<std::uint8_t> pixels_;
};

}  // namespace sh::debug
