// SPDX-License-Identifier: GPL-3.0-or-later
//
// Small text panels drawn over the picture: the free camera's key list, the frame-rate counter, the profiler.
// A panel is a few rows of "key<TAB>description" (a row without a tab is a heading over both columns): the keys
// are right-aligned so the descriptions start in one column. The rows are drawn once into a picture, white with a
// dark outline, and kept as an overlay texture until the text or its size changes.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace sh::hud {

struct PanelBitmap {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;  // straight alpha, rows top to bottom

    bool empty() const { return rgba.empty(); }
};

// The typeface of the panels. Rows are read byte by byte: the panels' text is Latin.
class PanelFont {
public:
    PanelFont();
    ~PanelFont();
    PanelFont(const PanelFont&) = delete;
    PanelFont& operator=(const PanelFont&) = delete;

    // Reads a TrueType file. False (the font stays unusable) when it cannot be read or is no font.
    bool load(const std::filesystem::path& file);
    bool ready() const;

    // The rows as one picture at the given text height in pixels; empty when the font is not ready or the
    // picture would be degenerate or wider or taller than 4096 pixels.
    PanelBitmap render(std::span<const std::string> rows, float pixel_height) const;

private:
    struct Face;
    std::unique_ptr<Face> face_;
};

// The font all panels share (gamedata/font/BarlowSemiCondensed-Regular.ttf), read on the first call. Call it from
// the game loop: the panels are drawn in the middle of a frame, where a file read would race the loader.
PanelFont& shared_font();
void load_shared_font();

// A panel on the screen: a translucent dark box with the rows on it.
class TextPanel {
public:
    // Where the box goes, in pixels of the picture, from its top left corner.
    struct Box {
        float x = 0.0f;
        float y = 0.0f;
    };

    // Renders the rows again only when they or the text height changed. False when there is nothing to draw.
    bool set(std::span<const std::string> rows, int pixel_height);
    // Size of the box with `padding` pixels around the text.
    float box_width(float padding) const { return static_cast<float>(width_) + 2.0f * padding; }
    float box_height(float padding) const { return static_cast<float>(height_) + 2.0f * padding; }
    void draw(Box box, float padding) const;

private:
    std::uint32_t texture_ = 0;
    int width_ = 0;
    int height_ = 0;
    int pixel_height_ = 0;
    std::string text_;
};

}  // namespace sh::hud
