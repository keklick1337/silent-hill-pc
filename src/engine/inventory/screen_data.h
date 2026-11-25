// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The inventory screen's static layout as data (gamedata/menu/inventory_screen.json): the
// box-title and field labels with their words and positions, the command words and the
// command column's rows and highlight bar, the scroll arrows and the layout on a wide window. A mod replaces the file
// whole (mods/<folder>/menu/inventory_screen.json). Standard library plus the shared JSON
// and mod helpers.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "engine/inventory/inventory_draw.h"
#include "engine/inventory/wide_layout.h"
#include "formats/common/status.h"

namespace sh::inventory {

struct ScreenLabelSpec {
    std::string text;
    Point at;
    bool boxed = false;
    Column column = Column::Centre;  // which column moves it on a wide window
};

struct CommandColumnSpec {
    std::int32_t label_x = 0;
    std::int32_t single_row_y = 0;
    std::int32_t upper_row_y = 0;
    std::int32_t lower_row_y = 0;
    std::int32_t row_height = 0;
    std::int32_t bar_left = 0;
    std::int32_t bar_right = 0;
    std::int32_t bar_half = 0;
    std::int32_t bar_top_two_rows = 0;
    std::int32_t bar_top_one_row = 0;
    Rgb bar_color;
};

struct ScrollArrowSpec {
    std::array<std::array<Point, 3>, 4> triangles{};
    std::int32_t blink_mask = 0;  // frame & mask is the blink phase
    std::int32_t blink_step = 0;  // brightness change per phase step
};

struct InventoryScreenData {
    std::array<ScreenLabelSpec, 8> labels;  // indexed by ScreenLabel, drawn in `label_order`
    std::array<ScreenLabel, 8> label_order{};
    std::array<std::string, 8> command_words;  // indexed by CommandLabel
    CommandColumnSpec column;
    ScrollArrowSpec arrows;
    WideRules wide;  // optional "wide" block; the built-in rules when it is missing
};

// The original screen's values (what gamedata/menu/inventory_screen.json ships).
const InventoryScreenData& default_screen_data();

Result<InventoryScreenData> parse_screen_data(std::string_view json_text);

// Reads menu/inventory_screen.json from the last mod (in load order) that has it, else from
// the base gamedata. `source` receives the path read.
Result<InventoryScreenData> load_screen_data(const std::filesystem::path& gamedata_dir,
                                             const std::filesystem::path& mods_dir, std::string* source = nullptr);

CommandColumn layout_command_column(const InventoryScreenData& data, std::uint8_t command, bool on_off_hidden,
                                    bool active, int row);
std::array<GradientTriangle, 4> scroll_arrows(const InventoryScreenData& data, std::int32_t frame);

}  // namespace sh::inventory
