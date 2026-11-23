// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the inventory screen's drawing records (sh::inventory::layout_command_column,
// scroll_arrows); expected values are the original screen's layout.

#include "engine/inventory/inventory_draw.h"
#include "engine/inventory/inventory.h"

#include "framework/sh_test.h"

using namespace sh::inventory;

SH_TEST(inventory_command_column_layout) {
    CommandColumn column = layout_command_column(static_cast<std::uint8_t>(Command::EquipReload), false, true, 1);
    SH_CHECK(column.label_count == 2 && column.labels[0].label == CommandLabel::Equip);
    SH_CHECK(column.labels[0].at == (Point{222, -42}) && column.labels[1].at == (Point{222, -26}));
    SH_CHECK(column.bar[0].corners[0] == (Point{56, -138}) && column.bar[0].corners[3] == (Point{136, -132}));
    SH_CHECK(column.bar[1].corners[1] == (Point{56, -126}));
    SH_CHECK(column.bar[0].colors[1] == (Rgb{0, 192, 64}) && column.bar[1].colors[1] == (Rgb{0, 0, 0}));
    column = layout_command_column(static_cast<std::uint8_t>(Command::OnOff), true, false, 0);
    SH_CHECK(column.label_count == 0 && !column.active);
    column = layout_command_column(static_cast<std::uint8_t>(Command::UseHealth), false, true, 0);
    SH_CHECK(column.label_count == 1 && column.labels[0].at == (Point{222, -34}));
    SH_CHECK(column.bar[0].corners[0] == (Point{56, -146}));
}

SH_TEST(inventory_scroll_arrows_blink) {
    const auto arrows = scroll_arrows(0x23);  // phase 3
    SH_CHECK(arrows[0].corners[0] == (Point{-60, 12}) && arrows[3].corners[2] == (Point{52, 20}));
    SH_CHECK(arrows[1].colors[0] == (Rgb{231, 231, 231}) && arrows[1].colors[2] == (Rgb{24, 24, 24}));
}

SH_TEST(inventory_background_panels_and_outline) {
    OutlineTrail trail;
    BackgroundInput in;
    in.selection = Selection::Exit;
    in.previous = Selection::Items;
    in.selection_age = 8;
    in.difficulty = 1;
    in.step = 1;
    in.frame = 0x50;  // pulse 47
    Background bg = layout_background(trail, in);
    SH_CHECK(bg.panel_count == 12 && !bg.glare && bg.corner_count == 6);
    // Lower row, middle box (Exit) pulses; others keep the hard-mode colour.
    SH_CHECK(bg.panels[8].corners[0] == (Point{-44, 192}) && bg.panels[8].colors[1] == (Rgb{175, 141, 192}));
    SH_CHECK(bg.panels[0].colors[1] == (Rgb{128, 0, 192}) && bg.panels[1].colors[0] == (Rgb{128, 0, 192}));
    // Settled: the outline is exactly the Exit frame.
    SH_CHECK(trail.quads[0][0] == (Point{-48, 185}) && trail.quads[0][2] == (Point{48, 209}));
    SH_CHECK(bg.outline[0].from == (Point{-48, 185}) && bg.outline[0].from_color == (Rgb{96, 96, 255}));
    SH_CHECK(bg.outline[5].layer == Layer::Panels && bg.outline[5].from == (Point{48, 186}));
    SH_CHECK(bg.corners[0].from == (Point{-50, 181}) && bg.corners[0].to == (Point{-2, 181}));
    SH_CHECK(bg.corners[5].to == (Point{50, 201}) && bg.corners[0].from_color == (Rgb{129, 129, 255}));

    // Half-way: the outline lies between the two frames; the trail keeps the old one.
    in.selection_age = 4;
    in.step = 20;
    bg = layout_background(trail, in);
    SH_CHECK(bg.panel_count == 0 && bg.corner_count == 0);
    SH_CHECK(trail.quads[1][0] == (Point{-48, 185}));
    SH_CHECK(trail.quads[0][0] == (Point{-41, 66}));
}

SH_TEST(inventory_background_examine_glare) {
    OutlineTrail trail;
    BackgroundInput in;
    in.selection = in.previous = Selection::Examine;
    in.selection_age = 0;
    in.glare_time = 40;
    Background bg{};
    for (int i = 0; i < 4; i++)
        bg = layout_background(trail, in);
    SH_CHECK(bg.glare && bg.picture_count == 2);
    SH_CHECK(bg.glare_quad.grey == (std::array<std::uint8_t, 4>{128, 192, 0, 128}));
    SH_CHECK(bg.glare_quad.corners[0] == (Point{-47, -198}) && bg.glare_quad.corners[3] == (Point{144, 56}));
    SH_CHECK(bg.picture[0].corners[2] == (Point{48, -198}) && bg.picture[1].corners[0] == (Point{47, -198}));
    in.glare_time = 10;
    bg = layout_background(trail, in);
    SH_CHECK(bg.picture_count == 0 && bg.glare_quad.grey == (std::array<std::uint8_t, 4>{160, 80, 254, 160}));
}

namespace {

class FixedRandom final : public HealthRandom {
public:
    std::int32_t value = 0, row = 0;
    int calls = 0;
    std::int32_t next() override { calls++; return value; }
    std::int32_t glitch_row() override { return row; }
};

}  // namespace

SH_TEST(inventory_health_status_scan_and_pulse) {
    HealthMonitor m;
    m.scan = 32;
    m.scan_mark = -300;
    FixedRandom random;
    // Full health: no glitch roll at all; scan line visible from row 30, the pulse marks it.
    HealthStatus hs = layout_health_status(m, 100 << 12, random);
    SH_CHECK(random.calls == 0 && hs.strip_count == 2 && m.scan == 33);
    SH_CHECK(hs.strips[1].corners[0] == (Point{-136, -181}) && hs.strips[1].uv[1] == (Point{40, 108}));
    SH_CHECK(m.scan_mark == -181 && hs.bar_count == 2 && m.pulse == 1);
    SH_CHECK(hs.bars[0].colors[0] == (Rgb{0, 255, 0}) && hs.bars[0].corners[1] == (Point{-136, -181}));
    SH_CHECK(hs.bars[1].corners[1] == (Point{-136, -197}) && hs.glow.corners[0] == (Point{-136, -179}));

    // Badly hurt: the scan runs 3 rows a frame, a glitch row shows for three frames.
    m = HealthMonitor{};
    random.value = 0;
    random.row = 50;
    hs = layout_health_status(m, 5 << 12, random);
    SH_CHECK(hs.strip_count == 2 && hs.strips[1].part == 2 && m.glitch == 1 && m.glitch_row == 50);
    SH_CHECK(hs.strips[1].corners[0] == (Point{-136, -134}) && hs.strips[1].uv[0] == (Point{40, 153}));
    SH_CHECK(hs.bar_count == 0 && m.pulse == 3 && m.scan == 3);
}

SH_TEST(inventory_results_save_dialog) {
    ResultsSave dialog = layout_results_save(1, -1);
    SH_CHECK(dialog.lines[0].from == (Point{-104, -72}) && dialog.lines[0].to == (Point{-104, -8}));
    SH_CHECK(dialog.lines[0].color == (Rgb{96, 96, 255}) && dialog.lines[8].color == (Rgb{224, 224, 255}));
    // The answer frame sits under "No" (102 to the right of "Yes").
    SH_CHECK(dialog.lines[12].from == (Point{26, 6}) && dialog.lines[23].color == (Rgb{160, 160, 255}));
    SH_CHECK(dialog.label_count == 4 && dialog.labels[3].label == ResultsLabel::Normal && dialog.labels[0].large);
    dialog = layout_results_save(0, 1);
    SH_CHECK(dialog.labels[3].label == ResultsLabel::Hard && dialog.labels[3].at == (Point{136, 240}));
    SH_CHECK(layout_results_save(0, 5).label_count == 3);
}
