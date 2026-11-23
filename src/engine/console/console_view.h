// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Turns a Console's state into a draw list of textured/solid quads (all from the one FontAtlas texture), and
// maps mouse positions back to scrollback text. No GL, no SDL: a backend uploads FontAtlas::pixels() once
// (re-uploading when the atlas is rebuilt) and draws `quads` with alpha blending.
//
// Layout (top-down drop-down panel, pixels, origin top-left):
//   [scrollback rows .............................]  newest at the bottom, wrapped to the panel width
//   [> prompt line with caret .....................]
//   [usage signature: name <req> [opt]  - arg help ]  the active argument highlighted
//   [candidate popup rows (up to 10) + "n/N"      ]  below the prompt, over the game picture

#pragma once

#include "engine/console/console.h"
#include "engine/console/font_atlas.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace sh::console {

struct Quad {
    float x = 0, y = 0, w = 0, h = 0;
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    std::uint32_t rgba = 0xFFFFFFFFu;  // 0xRRGGBBAA, straight alpha
};

struct DrawList {
    std::vector<Quad> quads;
};

struct ViewParams {
    float width = 1280;       // target size in pixels
    float height = 720;
    float slide = 1.0f;       // 0 = fully above the top edge, 1 = at rest
    float panel_fraction = 0.5f;  // height of the scrollback panel as a fraction of the target
    double time_seconds = 0;  // drives the caret blink
    int max_popup_rows = 10;
    bool show_caret = true;
};

class ConsoleView {
public:
    // Builds the draw list for this frame and remembers the geometry for hit_test(). Clamps Console::scroll().
    DrawList build(Console& console, const FontAtlas& font, const ViewParams& params);

    // The scrollback text under a point (pixels, top-left origin), from the last build(); the offset rounds to
    // the nearest character boundary. nullopt over the prompt, popup or outside the text.
    std::optional<TextPos> hit_test(float x, float y) const;

    // Like hit_test() for a drag in progress: a point above, below or beside the text takes the nearest row and
    // column, so a selection can run to the first or last visible line. nullopt only with no rows drawn.
    std::optional<TextPos> drag_test(float x, float y) const;

    // True when the point is over the panel, prompt or popup of the last build() (the game ignores clicks there).
    bool covers(float x, float y) const;

    int visible_rows() const { return visible_rows_; }
    int columns() const { return columns_; }
    float panel_bottom() const { return panel_bottom_; }   // y below the prompt row (popup starts here)

    // Colours by level (0xRRGGBBAA).
    static std::uint32_t level_colour(Level level);

private:
    struct Row {
        std::uint64_t line = 0;
        std::size_t begin = 0;    // byte span of the row in the line text
        std::size_t end = 0;
        Level level = Level::Plain;
        float y = 0;              // filled while drawing
    };
    std::vector<Row> drawn_;      // rows drawn last frame, top to bottom
    float panel_x_ = 0, panel_y_ = 0, panel_w_ = 0, panel_h_ = 0;
    float text_x_ = 0, cell_w_ = 1, cell_h_ = 1;
    float popup_y_ = 0, popup_h_ = 0, popup_x_ = 0, popup_w_ = 0;
    float panel_bottom_ = 0;
    int visible_rows_ = 0;
    int columns_ = 80;
    const Console* console_ = nullptr;
};

}  // namespace sh::console
