// SPDX-License-Identifier: GPL-3.0-or-later
//
// The inventory screen on a wide window (engine/inventory/wide_layout.h): how far the columns move and the ring
// spreads, and the frames of the outline that follow them.

#include "framework/sh_test.h"

#include "engine/inventory/inventory_draw.h"
#include "engine/inventory/item_view.h"
#include "engine/inventory/wide_layout.h"

using namespace sh::inventory;

SH_TEST(wide_layout_keeps_4_3_and_off_rules) {
    const WideRules rules;
    SH_CHECK(!wide_layout(rules, 1.0f).wide());
    SH_CHECK(!wide_layout(rules, 0.8f).wide());  // a taller window is letterboxed, not laid out again
    WideRules off;
    off.enabled = false;
    SH_CHECK(!wide_layout(off, 16.0f / 9.0f / (4.0f / 3.0f)).wide());
}

SH_TEST(wide_layout_moves_the_side_columns) {
    const WideRules rules;  // 0.75 of the room, at most 96, 20 from the edge
    const WideLayout w169 = wide_layout(rules, (16.0f / 9.0f) / (4.0f / 3.0f));
    SH_CHECK(w169.extra == 53);
    SH_CHECK(w169.shift == 40);
    SH_CHECK(w169.x(Column::Left, -100) == -140 && w169.x(Column::Right, 100) == 140);
    SH_CHECK(w169.x(Column::Centre, 7) == 7 && w169.x(Column::CentreRight, 0) == 20);
    SH_CHECK(w169.ring > 1.24f && w169.ring < 1.26f);

    // A phone at 20:9 hits the cap; the outer brackets keep the margin from the edge.
    const WideLayout phone = wide_layout(rules, (20.0f / 9.0f) / (4.0f / 3.0f));
    SH_CHECK(phone.shift == 80);
    // The touch controls' corner buttons keep the columns further in.
    const WideLayout touch = wide_layout(rules, (20.0f / 9.0f) / (4.0f / 3.0f), true);
    SH_CHECK(touch.shift == 106 + 160 - 146 - 64);
    WideRules wide = rules;
    wide.column_spread = 1.0;
    wide.max_column_shift = 1000;
    const WideLayout capped = wide_layout(wide, 2.0f);
    SH_CHECK(capped.extra == 160 && capped.shift == 160 + 160 - 146 - 20);
}

SH_TEST(wide_layout_columns_of_the_selections) {
    SH_CHECK(column_of(Selection::Option) == Column::Left && column_of(Selection::Health) == Column::Left);
    SH_CHECK(column_of(Selection::Map) == Column::Right && column_of(Selection::ItemCommands) == Column::Right);
    SH_CHECK(column_of(Selection::Items) == Column::Centre && column_of(Selection::Exit) == Column::Centre);
    SH_CHECK(column_of(Selection::Examine) == Column::CentreRight);
}

SH_TEST(wide_layout_moves_panels_and_outline) {
    OutlineTrail plain_trail, wide_trail;
    BackgroundInput in;
    in.selection = in.previous = Selection::Map;
    in.selection_age = 8;
    const Background plain = layout_background(plain_trail, in);
    in.column_shift = 40;
    const Background wide = layout_background(wide_trail, in);
    // Panels: left column out to the left, middle in place, right out to the right (row 0: 0..5, two halves each).
    SH_CHECK(wide.panels[0].corners[0].x == plain.panels[0].corners[0].x - 40);
    SH_CHECK(wide.panels[2].corners[0].x == plain.panels[2].corners[0].x);
    SH_CHECK(wide.panels[4].corners[0].x == plain.panels[4].corners[0].x + 40);
    // The settled outline and its brackets frame the moved Map box, the same size.
    SH_CHECK(wide_trail.quads[0][0].x == plain_trail.quads[0][0].x + 40);
    const auto width = [](const OutlineTrail& t) { return t.quads[0][2].x - t.quads[0][0].x; };
    SH_CHECK(width(wide_trail) == width(plain_trail));
    SH_CHECK(wide.corners[0].from.x == plain.corners[0].from.x + 40);
}

SH_TEST(wide_layout_spreads_the_item_row_only) {
    ScreenMatrix row, hand;
    row.m = {{{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}}};
    row.t = {1000, 0, 4000};
    hand = row;
    ScreenFit fit;
    fit.in_menu = true;
    fit.row_spread = 1.5f;
    fit_to_screen(row, 2, fit);
    fit_to_screen(hand, 7, fit);
    SH_CHECK(row.t[0] == 1500 && row.m[0][0] == 4096);  // moved, not grown
    SH_CHECK(hand.t[0] == 1000);
}
