// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Pure core of the controls quick-reference screen: which
// category every input action belongs to, how a binding is shortened to fit a
// cell, and how the rows are flowed into columns on one screen. No game state and
// no renderer; src/ui/screens/pc_controls_ref.cpp feeds it the live bindings and
// draws the result.

#pragma once

#include "engine/input/input_actions.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::input::ref {

enum class Category : int { Movement, Combat, Camera, Menu, System, Debug, Count };

inline constexpr int kCategoryCount = static_cast<int>(Category::Count);

// Every action has a category; adding an action without one fails the unit test.
Category category_of(Action action);
// Text key of the category heading: ui.controls_ref.category.<name>.
std::string_view category_key(Category category);

// One printed line of the reference: already localized and already read from the
// live configuration.
struct Row {
    Category category = Category::Menu;
    std::string label;
    std::string keyboard;  // both slots, "-" when unbound
    std::string pad;
    bool enabled = true;   // false: does nothing with the current camera scheme
};

using Measure = std::function<int(std::string_view)>;

// Cuts text to max_w with "..." (the whole text when it fits, "" when even the
// ellipsis does not). Shortening key names ("Left Shift" -> "L-Shift") is the caller's job.
std::string fit_ellipsis(std::string_view text, int max_w, const Measure& measure);

// Two bind slots as one cell text: "A/B", "A" when only the first is set, "-" when none.
std::string join_slots(std::string_view first, std::string_view second, std::string_view unbound = "-");

struct Params {
    int width = 320;      // text space of the Nm_* draw layer
    int margin = 8;
    int col_gap = 8;
    int top = 24;         // first line of the body
    int bottom = 204;     // end of the body (exclusive)
    int row_h = 10;
    int min_col_w = 140;  // a column narrower than this is not worth having
    int label_pct = 46;   // label / keyboard / pad share of a column, in percent
    int keyboard_pct = 30;
};

struct Placed {
    enum class Kind { Heading, Row } kind = Kind::Row;
    int column = 0;
    int x = 0;                    // left edge of the column
    int y = 0;
    int col_w = 0;
    Category category = Category::Menu;  // heading: the category
    int row = -1;                 // Row: index into the input rows
    // Row: cell positions and widths inside the column (absolute x).
    int label_x = 0, label_w = 0, keyboard_x = 0, keyboard_w = 0, pad_x = 0, pad_w = 0;
};

struct Layout {
    int columns = 1;
    int col_w = 0;
    std::vector<Placed> items;
    bool truncated = false;       // rows were dropped: even the widest arrangement did not fit
};

// Rows are grouped by category in enum order (stable inside a category), each
// group opens with a heading, columns are filled top to bottom and balanced. The
// fewest columns that fit win; when none does the widest arrangement is used and
// the overflow is dropped.
Layout compute_layout(const std::vector<Row>& rows, const Params& params);

}  // namespace sh::input::ref
