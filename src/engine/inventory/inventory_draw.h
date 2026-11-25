// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// What the inventory screen draws, as records: the
// command column and its highlight bar, the blinking scroll arrows, the box panels and the
// selection outline. The engine decides
// positions and colours from the original screen's layout; the game bridge turns the records
// into primitives and strings. Standard library only.

#pragma once

#include <array>
#include <cstdint>

#include "engine/inventory/inventory_menu.h"

namespace sh::inventory {

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb&) const = default;
};

struct Point {
    std::int32_t x = 0, y = 0;
    bool operator==(const Point&) const = default;
};

// The words of the command column (the game supplies the text).
enum class CommandLabel : std::uint8_t { Use, Equip, Unequip, Reload, Detail, On, Off, Look };

struct LabelDraw {
    Point at;
    CommandLabel label = CommandLabel::Use;
};

// A four-corner gradient quad (corner order: top left, bottom left, top right, bottom right).
struct GradientQuad {
    std::array<Point, 4> corners{};
    std::array<Rgb, 4> colors{};
};

struct GradientTriangle {
    std::array<Point, 3> corners{};
    std::array<Rgb, 3> colors{};
};

// The command column of the selected (or equipped) item.
struct CommandColumn {
    bool active = false;  // a command list is open: white text and the highlight bar
    int label_count = 0;
    std::array<LabelDraw, 2> labels{};
    std::array<GradientQuad, 2> bar{};  // upper and lower half of the highlight, when active
};

// `command`: the slot's command; `on_off_hidden`: an On/Off item that cannot be switched now
// shows no words; `row`: the highlighted row.
CommandColumn layout_command_column(std::uint8_t command, bool on_off_hidden, bool active, int row);

// The two double arrows beside the item carousel; `frame`: the game's frame counter.
std::array<GradientTriangle, 4> scroll_arrows(std::int32_t frame);

// --- The results screen's save question (the game's Gfx_Results_Save).

struct FlatLine {
    Point from, to;
    Rgb color;
};

enum class ResultsLabel : std::uint8_t { SaveQuestion, YesNo, NextGameMode, Normal, Hard };

struct ResultsLabelDraw {
    ResultsLabel label = ResultsLabel::SaveQuestion;
    Point at;
    int letters = 0;      // letters drawn
    bool large = false;   // the save question uses the high-resolution glyphs
};

struct ResultsSave {
    std::array<FlatLine, 24> lines{};  // the dialog frame, then the frame of the chosen answer
    int label_count = 0;
    std::array<ResultsLabelDraw, 4> labels{};
};

// `answer`: 0 yes, 1 no; `next_difficulty`: the next game's difficulty (-1 easy .. 1 hard).
ResultsSave layout_results_save(std::int32_t answer, std::int32_t next_difficulty);

// --- The health status: Harry's portrait with a moving scan line, glitches and a pulse bar
// coloured by health (the game's Gfx_Inventory_HealthStatusDraw).

struct HealthMonitor {
    std::int16_t scan = 0;          // scan line row, 0..163
    std::int16_t pulse = 0;         // pulse of the health bar, 0..63
    std::int16_t scan_mark = -300;  // where the scan line was when the pulse started; -300 none
    std::int16_t glitch = 0;        // frame of a glitch strip, 0..3
    std::int16_t glitch_row = 0;
};

// The screen's random numbers (the game's generator, called in the game's order).
class HealthRandom {
public:
    virtual ~HealthRandom() = default;
    virtual std::int32_t next() = 0;        // a 16-bit random number
    virtual std::int32_t glitch_row() = 0;  // a random row 0..133
};

// A strip of the portrait: `part` 0 the whole picture, 1 the scan line, 2 a glitch row.
struct PortraitStrip {
    int part = 0;
    std::array<Point, 4> corners{};
    std::array<Point, 4> uv{};
};

struct HealthStatus {
    int strip_count = 0;
    std::array<PortraitStrip, 3> strips{};
    GradientQuad glow;  // semi-transparent glow below the scan line
    int bar_count = 0;  // 2 while a pulse runs
    std::array<GradientQuad, 2> bars{};
};

// One frame of the health status; `health` is Q12.
HealthStatus layout_health_status(HealthMonitor& monitor, std::int32_t health, HealthRandom& random);

// --- The screen's fixed labels (the game's Gfx_ItemScreens_DrawInit).

enum class ScreenLabel : std::uint8_t { Equipment, Exit, Option, Map, Command, Status, Number, Name };

struct ScreenLabelDraw {
    ScreenLabel label = ScreenLabel::Equipment;
    Point at;
    bool boxed = false;  // a box title: a translation keeps the English word's centre
};

// The eight labels in the order the game draws them.
const std::array<ScreenLabelDraw, 8>& screen_labels();

// --- The screen's panels and the selection outline (the game's Gfx_Inventory_2dBackgroundDraw).

// Where a record goes: over the screen (outline), the panel layer, or the picture layer below it.
enum class Layer : std::uint8_t { Overlay, Panels, Picture };

struct LineDraw {
    Point from, to;
    Rgb from_color, to_color;
    Layer layer = Layer::Overlay;
};

// A semi-transparent grey glare over the examined item's picture (corner order as GradientQuad).
struct GlareQuad {
    std::array<Point, 4> corners{};
    std::array<std::uint8_t, 4> grey{};
};

// One half of an examine picture: `page` 0 is the left half, 1 the right one; `uv` are the
// texel corners in the page (same corner order as GradientQuad).
struct PictureHalf {
    int page = 0;
    std::array<Point, 4> corners{};
    std::array<Point, 4> uv{};
};

// The outline that frames the selected element, with the three previous frames' outlines
// it leaves behind. Corners go around: top left, bottom left, bottom right, top right.
struct OutlineTrail {
    std::array<std::array<Point, 4>, 4> quads{};  // [0] this frame, [3] the oldest
};

struct BackgroundInput {
    Selection selection = Selection::Items;
    Selection previous = Selection::Items;
    std::int32_t selection_age = 0;  // MenuState::selection_age
    std::int32_t difficulty = 0;     // -1 easy, 0 normal, 1 hard
    std::int32_t step = 0;           // the screen step (ScreenStep)
    std::int32_t frame = 0;          // the game's frame counter
    std::int32_t glare_time = 0;     // frames of the examine glare, 0..64
    std::int32_t column_shift = 0;   // a wide window moves the side columns out by this (wide_layout.h)
};

struct Background {
    int panel_count = 0;                   // 12, or 0 while the screen closes
    std::array<GradientQuad, 12> panels{}; // label bands of the upper and lower boxes
    bool glare = false;
    GlareQuad glare_quad;
    int picture_count = 0;                 // 0 or 2
    std::array<PictureHalf, 2> picture{};
    std::array<LineDraw, 24> outline{};    // the trail, newest first
    int corner_count = 0;                  // 6 once the outline has settled
    std::array<LineDraw, 6> corners{};
};

// Advances `trail` by one frame towards the selected element and lays out the panels, the glare
// and the outline in the order the game draws them.
Background layout_background(OutlineTrail& trail, const BackgroundInput& input);

}  // namespace sh::inventory
