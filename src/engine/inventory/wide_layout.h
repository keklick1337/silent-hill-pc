// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The inventory screen on a window wider than 4:3. The screen is three columns (status, equipment, commands over the
// item ring; Option, Exit, Map under it): on a wide window the side columns move out towards the edges and the ring's
// items spread apart, each piece keeping its own size and shape. The numbers come from the "wide" block of
// gamedata/menu/inventory_screen.json. Standard library only.

#pragma once

#include <cstdint>

#include "engine/inventory/inventory_menu.h"

namespace sh::inventory {

// Where a piece of the screen belongs. CentreRight is the examine picture, which spans the middle and right columns.
enum class Column : std::uint8_t { Left, Centre, Right, CentreRight };

struct WideRules {
    bool enabled = true;
    // The side columns move out by this share of the room past the 4:3 frame on each side...
    double column_spread = 0.75;
    // ...by at most this many screen units (the screen is 320 wide)...
    std::int32_t max_column_shift = 96;
    // ...and keep at least this much from the window's edge, more where the touch controls' corner buttons are.
    std::int32_t edge_margin = 20;
    std::int32_t edge_margin_touch = 64;
    // How far the item ring follows: 1 spreads the neighbours of the selected item as far as the columns move.
    double ring_spread = 0.6;
};

struct WideLayout {
    std::int32_t shift = 0;  // the side columns move out by this (screen units)
    std::int32_t extra = 0;  // room past the 4:3 frame on each side (screen units)
    float ring = 1.0f;       // the ring's sideways positions are multiplied by this

    std::int32_t x(Column column, std::int32_t x) const;
    bool wide() const { return shift != 0 || ring != 1.0f; }
};

// The x of a side column's centre in the 4:3 frame, measured from the middle.
inline constexpr std::int32_t kSideColumnCentre = 96;

// The layout for a window `wide_factor` 4:3 frames wide (1 or less: the original layout); `touch`: the touch controls
// are on the screen. Off rules give the original layout too.
WideLayout wide_layout(const WideRules& rules, float wide_factor, bool touch = false);

// The column of the box a selection frames.
Column column_of(Selection selection);

}  // namespace sh::inventory
