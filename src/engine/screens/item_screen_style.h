// SPDX-License-Identifier: GPL-3.0-or-later
// Small layout pieces of the item screens as data: gamedata/menu/item_screens.json (the rule at
// the top edge and the map notices of the inventory). A mod's menu/item_screens.json wins
// (newest mod first); a missing or refused file leaves the built-in default below.
#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <string_view>

#include "formats/common/status.h"

namespace sh::screens {

struct ItemScreenStyle {
    // Flat rectangle drawn over the top edge of every item screen: corners x0..x1, y0..y1.
    int rule_x0 = -160, rule_x1 = 160;
    int rule_y0 = -224, rule_y1 = -222;
    std::array<int, 3> rule_color{0, 0, 0};
    // "Too dark" / "no map" notice of the inventory's MAP entry (game font).
    int notice_x = 30, notice_y = 232;
    int notice_color = 7;  // menu font palette slot (kMenuTextColors names)
    // Game font strings ('_' = space, '\t' = indent); the drawn text still passes the menu
    // translation chokepoint, which knows them by these English strings.
    std::string notice_too_dark = "Too_dark_to_look_at\n\t\tthe_map_here.";
    std::string notice_no_map = "I_don't_have_the_map\n\t\tfor_this_place.";
};

Result<ItemScreenStyle> parse_item_screen_style(std::string_view json_text);
Result<ItemScreenStyle> load_item_screen_style(const std::filesystem::path& gamedata_dir,
                                               const std::filesystem::path& mods_dir, std::string* source);

}  // namespace sh::screens
