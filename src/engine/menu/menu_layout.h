// SPDX-License-Identifier: GPL-3.0-or-later
// sh::menu layout math: where the rows of a menu window go, which scroll arrows show, how a row's width
// is split between its label and its value column, and which row a pointer is over. Pure functions on
// integers (the views in src/ui/menu measure text and pass the widths in), so the arithmetic is tested
// without a renderer. Coordinates are the view's own space (the game's 320x240 menu space).
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::menu {

struct Metrics {
    int centre_x = 160;
    int header_y = 40;   // y of the centred title
    int rows_y = 64;     // y of the first visible row
    int pitch = 16;      // distance between rows
    int visible = 8;     // rows the window holds
    int left_x = 40;     // left edge of the marked label
    int right_x = 280;   // right edge of the value column
    int arrow_pad = 16;  // space the value arrows take on each side of the value text
    int gap = 12;        // minimum space between label and value
    int help_y = 212;       // y of a one-line help
    int help_bottom = 228;  // a help of several lines grows upward from here (its last glyph's bottom)
};

struct Slot {
    int row = 0;  // index into the controller's visible rows
    int y = 0;
    int h = 0;  // the row's height (the pitch, unless the rows have heights of their own)
};

struct Window {
    std::vector<Slot> slots;
    bool arrow_up = false;    // rows are hidden above the window
    bool arrow_down = false;  // ... and below
    int up_y = 0;             // vertical centre of the scroll arrows
    int down_y = 0;
};

// The slots of the rows top .. top + visible - 1 (as many as exist) and the scroll arrows.
Window compute_window(const Metrics& m, int row_count, int top);

// The row index under (x, y), or -1: rows span left_x .. right_x and y .. y + pitch.
int row_at_point(const Metrics& m, int row_count, int top, int x, int y);

// The same for rows of their own heights (`heights[i]` lines, each > 0) in a window of visible * pitch lines: the
// window holds the rows from `top` that fit whole. With every height the pitch both are the functions above.
// The first row of a window that ends with the last row (the largest top worth scrolling to).
int last_top(const Metrics& m, const std::vector<int>& heights);
Window compute_window(const Metrics& m, const std::vector<int>& heights, int top);
int row_at_point(const Metrics& m, const std::vector<int>& heights, int top, int x, int y);

struct Columns {
    int label_max = 0;  // width the label may take
    int value_max = 0;  // width the value may take (arrows excluded)
    bool label_cut = false;
    bool value_cut = false;
};

// Splits `total` (the row's width) between a label and a value of the given natural widths. The value
// is kept whole when it fits (it is short: On, Off, a number); otherwise each side gets a share in
// proportion to its width, never less than a third of the total. `arrows` reserves the arrow space.
Columns split_columns(const Metrics& m, int label_w, int value_w, bool arrows);

// Index of the item to start a dropdown window at so that `selected` is inside it, keeping `top` when it
// already is (the controller does the same for rows; the view needs it to place the list).
int keep_visible(int top, int selected, int count, int visible);

// The x range of a `width`-wide menu space shown centred in a window `wide` 4:3 frames wide (1 at 4:3
// or narrower): the extra room is split evenly left of 0 and right of `width`.
struct Span {
    int left = 0;
    int right = 0;
};
Span visible_span(int width, float wide);

// Word-wraps `text` into lines no wider than `max_w`, breaking at spaces and at '_' (the game font's
// space); the break is dropped at a line end. A word wider than `max_w` keeps a line of its own (the
// caller pans it). An empty text gives no lines.
std::vector<std::string> wrap_words(std::string_view text, int max_w,
                                    const std::function<int(std::string_view)>& width);

// Where a help of `lines` lines at `line_pitch` goes: a single line stays at help_y, more lines grow up
// from help_bottom, and the row window gives up the rows (and its scroll arrow) that would overlap.
struct HelpPlace {
    int first_y = 0;
    int visible = 0;  // rows the window may still hold (at least 1)
};
HelpPlace place_help(const Metrics& m, int lines, int line_pitch);

// The value column of a page of label / value rows. Widths are measured at full text size; the bar
// of a bar row does not scale with the text.
struct ColumnRow {
    int label = 0;     // label width
    int value = 0;     // value text width (beside the bar on a bar row); 0 with no bar: a row without a value
    bool bar = false;
    // The label gives way (it ends in "..." where it meets the column) and takes no part in sizing the page; a page
    // with such rows puts its column as far right as the values let it, so their labels get the room.
    bool yields = false;
};
struct ColumnInput {
    int label_x = 0;       // where labels start
    int right = 0;         // nothing is drawn past this
    int gap = 0;           // label end .. value start (the value's left arrow sits in it)
    int min_value_x = 0;   // the column never starts left of this (the retail look)
    int bar = 0;           // width of a bar and the space after it
    int min_scale = 4096;  // Q12: how far the page's text may shrink to fit
    int max_scale = 4096;  // Q12: the page's size so far this visit (it never grows back while the page is open)
    int note_min_scale = 0;  // Q12: how far a row without a value may shrink alone (0: as far as min_scale)
    std::vector<ColumnRow> rows;
};
struct ColumnLayout {
    int scale = 4096;  // Q12 text size of the page's rows: the largest at which one column holds every row with a value
    int value_x = 0;   // the page's value column (right when no row has a value); every value starts here
    bool fits = true;  // false: a row is too long even at its smallest size and pans
};
// Only rows with a value size the page: a row without one (a note such as "Changes apply after restart",
// which comes and goes as the player changes things) shrinks alone, so the page's text does not jump. A row
// too long to sit beside any column even at min_scale does not size the page either: it shrinks alone.
ColumnLayout options_columns(const ColumnInput& in);

// One row of that page: its value starts at the column whatever its label or value. A row whose label runs
// into the column or whose value runs past the right edge takes a smaller size of its own, down to
// note_min_scale, and pans what is still too long; a row without a value does the same across the whole row.
struct RowPlace {
    int value_x = 0;
    int label_max = 0;
    int scale = 4096;  // Q12 text size of this row
};
RowPlace place_row(const ColumnInput& in, const ColumnLayout& layout, const ColumnRow& row);

}  // namespace sh::menu
