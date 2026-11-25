// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The inventory screen's box panels, examine glare and selection outline as drawing records.

#include "engine/inventory/inventory_draw.h"

#include "engine/inventory/wide_layout.h"

#include "engine/world/fixed_trig.h"

namespace sh::inventory {

namespace {

// A box of the screen: top left corner and size.
struct Box {
    std::int16_t x, y, w, h;
};

// The frame of each selectable element, indexed by Selection.
constexpr std::array<Box, 9> kFrames = {{
    {-32, -52, 64, 128},     // item row
    {-48, -200, 96, 144},    // equipped item
    {-48, 185, 96, 24},      // Exit
    {-144, 185, 96, 24},     // Option
    {48, 185, 96, 24},       // Map
    {48, -200, 96, 128},     // item commands
    {48, -200, 96, 128},     // equipped item commands
    {-144, -200, 96, 156},   // health status
    {-48, -200, 192, 256},   // examine picture
}};

// Ends of the two corner brackets of the settled frame: the top left and the bottom right one.
struct Brackets {
    Point top_left, bottom_right;
};

constexpr std::array<Brackets, 9> kBrackets = {{
    {{-34, -56}, {34, 80}},
    {{-50, -204}, {50, -52}},
    {{-50, 181}, {50, 213}},
    {{-146, 181}, {-46, 213}},
    {{46, 181}, {146, 213}},
    {{46, -204}, {146, -68}},
    {{45, -204}, {146, -68}},
    {{-146, -204}, {-46, -40}},
    {{-50, -204}, {146, 60}},
}};

constexpr int kSettledAge = 8;
constexpr std::int32_t kClosingStep = 20;
constexpr std::int32_t kExamineOpeningStep = 15;
constexpr std::int32_t kGlareFadeStart = 33;

// The panels: three boxes in the upper row and three in the lower one, each band two gradient
// halves 6 lines high.
constexpr std::int32_t kPanelLeft = -140;
constexpr std::int32_t kPanelWidth = 88;
constexpr std::int32_t kPanelPitch = 96;
constexpr std::array<std::int32_t, 2> kPanelTop = {-192, 192};
constexpr std::int32_t kPanelHalf = 6;

constexpr Rgb kBlack{0, 0, 0};

// Which panel (row, column) belongs to the selected element; none for the item row and the
// examine picture.
bool highlighted_panel(Selection selection, int& row, int& column)
{
    switch (selection) {
        case Selection::EquippedItem: row = 0; column = 1; return true;
        case Selection::Exit: row = 1; column = 1; return true;
        case Selection::Option: row = 1; column = 0; return true;
        case Selection::Map: row = 1; column = 2; return true;
        case Selection::ItemCommands:
        case Selection::EquippedCommands: row = 0; column = 2; return true;
        case Selection::Health: row = 0; column = 0; return true;
        default: return false;
    }
}

// Panel colour by difficulty; the highlighted panel pulses (`pulse` 0..63).
Rgb panel_color(std::int32_t difficulty, bool highlighted, std::int32_t pulse)
{
    const auto rising = static_cast<std::uint8_t>(pulse - 128);
    const auto triple = static_cast<std::uint8_t>(pulse * 3);
    switch (difficulty) {
        case -1: return highlighted ? Rgb{rising, 192, triple} : Rgb{128, 192, 0};
        case 1: return highlighted ? Rgb{rising, triple, 192} : Rgb{128, 0, 192};
        default: return highlighted ? Rgb{triple, rising, 192} : Rgb{0, 128, 192};
    }
}

// How far the outline still is from the new element: the eased share (Q12) of the previous
// one, cos^2 of the selection age.
std::int32_t previous_share(std::int32_t age)
{
    const std::int32_t cosine = sh::world::fixed_cos(age << 7);
    return (cosine * cosine * 16) >> 16;
}

std::int32_t blend(std::int32_t to, std::int32_t from, std::int32_t share)
{
    return static_cast<std::int16_t>(to + (((from - to) * share) >> 12));
}

std::int32_t s16(std::int32_t v) { return static_cast<std::int16_t>(v); }

std::uint8_t glare_in(std::int32_t t, std::int32_t full, std::int32_t gain)
{
    return t >= full ? 254 : static_cast<std::uint8_t>(t * gain);
}

std::uint8_t glare_out(std::int32_t t, std::int32_t end, std::int32_t gain)
{
    return t < end ? static_cast<std::uint8_t>((end - t) * gain) : 0;
}

// A selection's frame, moved with its column on a wide window.
Box frame_of(Selection selection, std::int32_t shift)
{
    Box box = kFrames[static_cast<std::size_t>(selection)];
    WideLayout wide;
    wide.shift = shift;
    box.x = static_cast<std::int16_t>(wide.x(column_of(selection), box.x));
    return box;
}

}  // namespace

Background layout_background(OutlineTrail& trail, const BackgroundInput& in)
{
    Background out;
    WideLayout wide;
    wide.shift = in.column_shift;
    constexpr Column kPanelColumns[3] = {Column::Left, Column::Centre, Column::Right};
    const std::int32_t wave = in.frame & 0x7F;
    const std::int32_t pulse = wave < 64 ? wave : 127 - wave;
    const bool settled = in.selection_age == kSettledAge;

    int hi_row = -1, hi_column = -1;
    if (settled && !highlighted_panel(in.selection, hi_row, hi_column))
        hi_row = hi_column = -1;

    if (in.step != kClosingStep) {
        for (int row = 0; row < 2; row++) {
            for (int column = 0; column < 3; column++) {
                const Rgb color = panel_color(in.difficulty, row == hi_row && column == hi_column, pulse);
                for (int half = 0; half < 2; half++) {
                    GradientQuad& quad = out.panels[out.panel_count++];
                    const std::int32_t x0 = wide.x(kPanelColumns[column], kPanelLeft + kPanelPitch * column);
                    const std::int32_t x1 = x0 + kPanelWidth;
                    const std::int32_t y0 = kPanelTop[row] + kPanelHalf * half;
                    const std::int32_t y1 = y0 + kPanelHalf;
                    quad.corners = {Point{x0, y0}, Point{x0, y1}, Point{x1, y0}, Point{x1, y1}};
                    // Upper half fades in from black, lower half fades out to black.
                    quad.colors = half == 0 ? std::array<Rgb, 4>{kBlack, color, kBlack, color}
                                            : std::array<Rgb, 4>{color, kBlack, color, kBlack};
                }
            }
        }
    }

    for (int age = 3; age > 0; age--)
        trail.quads[age] = trail.quads[age - 1];

    const Box to = frame_of(in.selection, in.column_shift);
    const Box from = frame_of(in.previous, in.column_shift);
    const std::int32_t share = previous_share(in.selection_age);
    const std::int32_t left = blend(to.x, from.x, share);
    const std::int32_t top = blend(to.y, from.y, share);
    const std::int32_t bottom = blend(to.y + to.h, from.y + from.h, share);
    const std::int32_t right = blend(to.x + to.w, from.x + from.w, share);
    trail.quads[0] = {Point{left, top}, Point{left, bottom}, Point{right, bottom}, Point{right, top}};

    const bool examining = in.selection == Selection::Examine;
    if (examining || in.step == kExamineOpeningStep) {
        const auto& q = trail.quads[3];
        const std::int32_t t = in.glare_time;
        out.glare = true;
        out.glare_quad.corners = {Point{q[0].x + 1, q[0].y + 2}, Point{q[1].x + 1, q[1].y},
                                  Point{q[3].x, q[3].y + 2}, Point{q[3].x, q[1].y}};
        // The glare brightens corner by corner, then fades corner by corner.
        if (t < kGlareFadeStart)
            out.glare_quad.grey = {glare_in(t, 16, 16), glare_in(t, 32, 8), glare_in(t, 8, 32), glare_in(t, 16, 16)};
        else
            out.glare_quad.grey = {glare_out(t, 48, 16), glare_out(t, 64, 8), glare_out(t, 40, 32), glare_out(t, 48, 16)};

        // The picture shows under the glare once it fades (examine) or while it brightens (step 15).
        if ((t >= kGlareFadeStart && examining) || (t < kGlareFadeStart && in.step == kExamineOpeningStep)) {
            const std::int32_t mid_top = q[0].x + ((-(q[0].x + 4) + q[3].x) >> 1);
            const std::int32_t mid_bottom = q[1].x + ((-(q[1].x + 4) + q[3].x) >> 1);
            PictureHalf& left_half = out.picture[0];
            left_half.page = 0;
            left_half.uv = {Point{0, 16}, Point{0, 255}, Point{160, 16}, Point{160, 255}};
            left_half.corners = {Point{q[0].x + 1, q[0].y + 2}, Point{q[1].x + 1, q[1].y},
                                 Point{s16(mid_top + 2), q[3].y + 2}, Point{s16(mid_bottom + 2), q[1].y}};
            PictureHalf& right_half = out.picture[1];
            right_half.page = 1;
            right_half.uv = {Point{32, 16}, Point{32, 255}, Point{192, 16}, Point{192, 255}};
            right_half.corners = {Point{s16(mid_top + 1), q[0].y + 2}, Point{s16(mid_bottom + 1), q[1].y},
                                  Point{q[3].x, q[3].y + 2}, Point{q[3].x, q[1].y}};
            out.picture_count = 2;
        }
    }

    int n = 0;
    for (int age = 0; age < 4; age++) {
        const auto shade = static_cast<std::uint8_t>(0x60 - 0x20 * age);
        const Rgb color{shade, shade, 255};
        const auto& q = trail.quads[age];
        for (int i = 0; i < 4; i++)
            out.outline[n++] = {q[i], q[(i + 1) & 3], color, color, Layer::Overlay};
        // The two horizontal edges are doubled one line lower on the panel layer.
        for (int i = 1; i < 4; i += 2) {
            const Point& a = q[i];
            const Point& b = q[(i + 1) & 3];
            out.outline[n++] = {Point{a.x, a.y + 1}, Point{b.x, b.y + 1}, color, color, Layer::Panels};
        }
    }

    if (!settled)
        return out;

    const Brackets& br = kBrackets[static_cast<std::size_t>(in.selection)];
    const std::int32_t half_w = to.w >> 1;
    const std::int32_t half_h = to.h >> 1;
    const auto bright = static_cast<std::uint8_t>(-0x50 - pulse);
    const auto dim = static_cast<std::uint8_t>(pulse + 0x70);
    const Rgb start{bright, bright, 255};
    const Rgb end{dim, dim, 255};
    const Column column = column_of(in.selection);
    const Point a{wide.x(column, br.top_left.x), br.top_left.y};
    const Point b{wide.x(column, br.bottom_right.x), br.bottom_right.y};
    // Each bracket: a horizontal stroke drawn twice (one line apart) and a vertical stroke.
    out.corners[0] = {a, Point{a.x + half_w, a.y}, start, end, Layer::Overlay};
    out.corners[1] = {Point{a.x, a.y - 1}, Point{a.x + half_w, a.y - 1}, start, end, Layer::Overlay};
    out.corners[2] = {a, Point{a.x, a.y + half_h}, start, end, Layer::Overlay};
    out.corners[3] = {b, Point{b.x - half_w, b.y}, start, end, Layer::Overlay};
    out.corners[4] = {Point{b.x, b.y - 1}, Point{b.x - half_w, b.y - 1}, start, end, Layer::Overlay};
    out.corners[5] = {b, Point{b.x, b.y - half_h}, start, end, Layer::Overlay};
    out.corner_count = 6;
    return out;
}

}  // namespace sh::inventory
