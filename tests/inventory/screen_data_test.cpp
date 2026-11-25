// SPDX-License-Identifier: GPL-3.0-or-later
// gamedata/menu/inventory_screen.json equals the original screen's built-in layout, and bad files are refused.

#include <filesystem>
#include <fstream>
#include <iterator>

#include "engine/inventory/screen_data.h"
#include "framework/sh_test.h"

using namespace sh::inventory;

SH_TEST(inventory_screen_json_matches_defaults) {
    const auto loaded = load_screen_data(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata", {});
    SH_CHECK(static_cast<bool>(loaded));
    const InventoryScreenData& a = loaded.value();
    const InventoryScreenData& b = default_screen_data();
    for (std::size_t i = 0; i < 8; i++) {
        SH_CHECK(a.labels[i].text == b.labels[i].text);
        SH_CHECK(a.labels[i].at == b.labels[i].at);
        SH_CHECK(a.labels[i].boxed == b.labels[i].boxed);
        SH_CHECK(a.labels[i].column == b.labels[i].column);
        SH_CHECK(a.label_order[i] == b.label_order[i]);
        SH_CHECK(a.command_words[i] == b.command_words[i]);
    }
    SH_CHECK(a.column.label_x == b.column.label_x && a.column.bar_top_one_row == b.column.bar_top_one_row);
    SH_CHECK(a.column.bar_color == b.column.bar_color);
    SH_CHECK(a.arrows.triangles == b.arrows.triangles);
    SH_CHECK(a.arrows.blink_mask == b.arrows.blink_mask && a.arrows.blink_step == b.arrows.blink_step);
}

SH_TEST(inventory_screen_json_rejects_bad_files) {
    SH_CHECK(!parse_screen_data("{}"));
    SH_CHECK(!parse_screen_data("{\"bogus\": 1}"));
}
