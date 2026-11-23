// SPDX-License-Identifier: GPL-3.0-or-later
// The menu view's layout math (src/engine/menu/menu_layout.*): window slots, scroll arrows, hit testing,
// the label / value width split.

#include "engine/menu/menu_layout.h"
#include "framework/sh_test.h"

using namespace sh::menu;

SH_TEST(menu_layout_window_slots_and_arrows) {
    Metrics m;
    m.rows_y = 64;
    m.pitch = 16;
    m.visible = 4;
    Window w = compute_window(m, 10, 0);
    SH_CHECK(w.slots.size() == 4);
    SH_CHECK(w.slots[0].y == 64 && w.slots[3].y == 64 + 48);
    SH_CHECK(!w.arrow_up && w.arrow_down);
    w = compute_window(m, 10, 3);
    SH_CHECK(w.slots[0].row == 3 && w.arrow_up && w.arrow_down);
    w = compute_window(m, 10, 99);  // clamped to the last full window
    SH_CHECK(w.slots[0].row == 6 && w.arrow_up && !w.arrow_down);
    w = compute_window(m, 2, 0);
    SH_CHECK(w.slots.size() == 2 && !w.arrow_up && !w.arrow_down);
}

SH_TEST(menu_layout_row_hit_test) {
    Metrics m;
    m.rows_y = 64;
    m.pitch = 16;
    m.visible = 4;
    m.left_x = 40;
    m.right_x = 280;
    SH_CHECK(row_at_point(m, 10, 2, 100, 64) == 2);
    SH_CHECK(row_at_point(m, 10, 2, 100, 64 + 16 * 3 + 15) == 5);
    SH_CHECK(row_at_point(m, 10, 2, 100, 64 + 16 * 4) == -1);  // below the window
    SH_CHECK(row_at_point(m, 10, 2, 20, 70) == -1);            // left of the rows
    SH_CHECK(row_at_point(m, 10, 2, 100, 40) == -1);           // above
    SH_CHECK(row_at_point(m, 2, 0, 100, 64 + 16 * 3) == -1);   // an empty slot
}

SH_TEST(menu_layout_split_columns) {
    Metrics m;
    m.left_x = 0;
    m.right_x = 240;
    m.gap = 12;
    m.arrow_pad = 16;
    Columns c = split_columns(m, 80, 40, true);  // fits whole
    SH_CHECK(!c.label_cut && !c.value_cut && c.label_max == 80 && c.value_max == 40);
    c = split_columns(m, 300, 30, true);  // a long label gives way, the value stays whole
    SH_CHECK(!c.value_cut && c.label_cut && c.value_max == 30);
    SH_CHECK(c.label_max + c.value_max + 12 + 32 == 240);
    c = split_columns(m, 300, 300, false);  // both long: both are cut and share the width
    SH_CHECK(c.label_cut && c.value_cut && c.label_max + c.value_max + 12 == 240);
    SH_CHECK(c.label_max >= 80 && c.value_max >= 80);
    c = split_columns(m, 100, 0, false);  // no value: the label has the whole row
    SH_CHECK(!c.label_cut && c.label_max == 100);
}

SH_TEST(menu_layout_keep_visible) {
    SH_CHECK(keep_visible(0, 3, 20, 5) == 0);
    SH_CHECK(keep_visible(0, 7, 20, 5) == 3);
    SH_CHECK(keep_visible(10, 4, 20, 5) == 4);
    SH_CHECK(keep_visible(18, 19, 20, 5) == 15);
}

// A wide window shows the 320-wide menu space centred: the extra room is split between both sides.
SH_TEST(menu_layout_visible_span_of_wide_windows) {
    const Span four_three = visible_span(320, 1.0f);
    SH_CHECK(four_three.left == 0 && four_three.right == 320);
    SH_CHECK(visible_span(320, 0.75f).right == 320);  // narrower than 4:3: letterboxed, no extra
    const Span sixteen_nine = visible_span(320, (16.0f / 9.0f) / (4.0f / 3.0f));
    SH_CHECK(sixteen_nine.left == -53 && sixteen_nine.right == 373);
    const Span ultra = visible_span(320, (32.0f / 9.0f) / (4.0f / 3.0f));
    SH_CHECK(ultra.left == -266 && ultra.right == 586);
}

namespace {

// 6 pixels a character, a fixed-width stand-in for the menu font.
int six_px(std::string_view s) { return static_cast<int>(s.size()) * 6; }

}  // namespace

SH_TEST(menu_layout_wrap_words) {
    auto lines = wrap_words("Changes_apply_after_restart", 78, six_px);  // the game font's '_' spaces
    SH_CHECK(lines.size() == 2 && lines[0] == "Changes_apply" && lines[1] == "after_restart");
    lines = wrap_words("one two three", 600, six_px);  // fits: one line, breaks kept
    SH_CHECK(lines.size() == 1 && lines[0] == "one two three");
    lines = wrap_words("  a  b  ", 600, six_px);  // leading and trailing breaks dropped, inner ones kept
    SH_CHECK(lines.size() == 1 && lines[0] == "a  b");
    lines = wrap_words("x /a/very/long/path/that/never/fits y", 60, six_px);  // an overlong word: a line of its own
    SH_CHECK(lines.size() == 3 && lines[1] == "/a/very/long/path/that/never/fits");
    SH_CHECK(wrap_words("", 60, six_px).empty());
    for (const std::string& l : wrap_words("the quick brown fox jumps over the lazy dog", 66, six_px)) {
        SH_CHECK(six_px(l) <= 66);
    }
}

SH_TEST(menu_layout_help_grows_upward) {
    Metrics m;
    m.rows_y = 42;
    m.pitch = 16;
    m.visible = 9;
    m.help_y = 200;
    m.help_bottom = 220;
    HelpPlace p = place_help(m, 1, 10);  // one line stays where the single help line always was
    SH_CHECK(p.first_y == 200 && p.visible == 9);
    p = place_help(m, 2, 10);  // two small lines still fit under a full window
    SH_CHECK(p.first_y == 200 && p.visible == 9);
    p = place_help(m, 3, 10);  // a third grows upward and takes a row
    SH_CHECK(p.first_y == 190 && p.visible == 8);
    p = place_help(m, 2, 16);  // full-size lines
    SH_CHECK(p.first_y == 188 && p.visible == 8);
    p = place_help(m, 40, 16);  // never fewer than one row
    SH_CHECK(p.visible == 1);
    SH_CHECK(place_help(m, 0, 10).visible == 9);
}

namespace {

ColumnInput page_4x3() {
    ColumnInput in;
    in.label_x = 64;
    in.right = 304;
    in.gap = 20;
    in.min_value_x = 168;
    in.bar = 54;
    in.min_scale = 3328;
    return in;
}

int up(int w, int s) { return (w * s + 4095) >> 12; }

// Every row of the page is whole and in the one column: the label ends before its value, the value starts at
// the column and ends before the right edge.
bool all_whole(const ColumnInput& in, const ColumnLayout& c) {
    for (const ColumnRow& r : in.rows) {
        const RowPlace p = place_row(in, c, r);
        if (up(r.label, p.scale) > p.label_max) return false;
        const int v = (r.bar ? in.bar : 0) + up(r.value, p.scale);
        if ((r.bar || r.value > 0) && (p.value_x != c.value_x || p.value_x + v > in.right)) return false;
    }
    return true;
}

}  // namespace

SH_TEST(menu_layout_options_columns) {
    ColumnInput in = page_4x3();
    in.rows = {{60, 30, false}, {50, 20, false}};
    ColumnLayout c = options_columns(in);  // short texts: the retail column at full size
    SH_CHECK(c.fits && c.scale == 4096 && c.value_x == 168 && all_whole(in, c));

    in.rows = {{150, 30, false}, {60, 30, false}};
    c = options_columns(in);  // a longer label pushes the column right
    SH_CHECK(c.fits && c.scale == 4096 && c.value_x == 64 + 150 + 20 && all_whole(in, c));

    // A long label with a short value and a short label with a long value: no one column holds both at full
    // size, so the page shrinks until one does; no row leaves the column.
    in.right = 357;
    in.rows = {{180, 25, false}, {60, 140, false}, {100, 30, false}};
    c = options_columns(in);
    SH_CHECK(c.fits && c.scale < 4096 && c.scale >= 3328 && all_whole(in, c));
    SH_CHECK(place_row(in, c, in.rows[1]).scale == c.scale && place_row(in, c, in.rows[0]).scale == c.scale);
    in.right = 304;

    in.rows = {{170, 80, false}, {60, 30, false}};  // 170 + 20 + 80 > 240: shrinks a little, all whole
    c = options_columns(in);
    SH_CHECK(c.fits && c.scale < 4096 && c.scale >= 3328 && all_whole(in, c));

    in.right = 357;  // a 16:9 window: the same page fits at full size
    c = options_columns(in);
    SH_CHECK(c.fits && c.scale == 4096 && all_whole(in, c));

    in = page_4x3();
    in.rows = {{300, 80, false}, {60, 30, false}};  // too long even at the smallest size: pans, no shrinking
    c = options_columns(in);
    SH_CHECK(!c.fits && c.scale == 4096);
    RowPlace p = place_row(in, c, in.rows[0]);  // its value stays in the column; the label shrinks alone and pans
    SH_CHECK(c.value_x == 168 && p.value_x == 168 && p.label_max == 168 - 20 - 64 && p.scale == in.min_scale);
    in.rows = {{70, 300, false}, {60, 30, false}};
    c = options_columns(in);
    p = place_row(in, c, in.rows[0]);  // a short label stays whole at the column, the long value pans
    SH_CHECK(p.value_x == c.value_x && p.label_max >= 70);

    in = page_4x3();
    in.rows = {{130, 40, true}, {80, 0, true}};  // the bar keeps its width, only the text shrinks
    c = options_columns(in);
    SH_CHECK(c.fits && c.scale < 4096 && all_whole(in, c));

    in = page_4x3();
    in.rows = {{260, 0, false}};  // an info line without a value may use the whole row, shrinking alone
    c = options_columns(in);
    p = place_row(in, c, in.rows[0]);
    SH_CHECK(c.fits && c.scale == 4096 && c.value_x == 304 && p.label_max == 240);
    SH_CHECK(p.scale < 4096 && p.scale >= 3328 && up(260, p.scale) <= 240);
}

SH_TEST(menu_layout_info_rows_do_not_size_the_page) {
    // A mod's page: switching the mod on adds notes ("Changes apply after restart", "Overridden by later
    // mods") that are longer than any row with a value. The page's text keeps its size and column.
    ColumnInput in = page_4x3();
    in.right = 357;
    in.label_x = 24;
    in.rows = {{65, 30, false}, {80, 0, false}, {90, 0, false}, {70, 38, false}};
    const ColumnLayout before = options_columns(in);
    SH_CHECK(before.fits && before.scale == 4096);
    in.rows.insert(in.rows.begin() + 3, ColumnRow{340, 0, false});  // longer than the row at full size
    const ColumnLayout after = options_columns(in);
    SH_CHECK(after.scale == before.scale && after.value_x == before.value_x && after.fits);
    const RowPlace note = place_row(in, after, in.rows[3]);
    SH_CHECK(note.scale < 4096 && up(340, note.scale) <= 357 - 24);
    SH_CHECK(place_row(in, after, in.rows[0]).scale == 4096);

    // A note too long even at the smallest size pans at that size; the page is still whole.
    in.rows[3].label = 900;
    const ColumnLayout pans = options_columns(in);
    SH_CHECK(pans.scale == 4096 && !pans.fits && place_row(in, pans, in.rows[3]).scale == in.min_scale);

    // A note may go down to its own smallest size (the help line's) before it pans.
    in = page_4x3();
    in.rows = {{60, 30, false}, {300, 0, false}};
    SH_CHECK(!options_columns(in).fits);
    in.note_min_scale = 2560;
    const ColumnLayout small = options_columns(in);
    const RowPlace p = place_row(in, small, in.rows[1]);
    SH_CHECK(small.fits && small.scale == 4096 && p.scale < in.min_scale && up(300, p.scale) <= 240);
}

SH_TEST(menu_layout_page_size_kept_for_the_visit) {
    // A value that shrank the page once keeps it at that size while the page is open: no jumping back.
    ColumnInput in = page_4x3();
    in.rows = {{170, 80, false}, {60, 30, false}};
    const ColumnLayout shrunk = options_columns(in);
    SH_CHECK(shrunk.scale < 4096);
    in.rows[0].value = 20;
    in.max_scale = shrunk.scale;
    const ColumnLayout kept = options_columns(in);
    SH_CHECK(kept.scale == shrunk.scale && all_whole(in, kept));
    in.max_scale = 4096;  // a new visit starts at full size
    SH_CHECK(options_columns(in).scale == 4096);
}
