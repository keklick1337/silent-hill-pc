// SPDX-License-Identifier: GPL-3.0-or-later
//
// gamedata/menu/text_style.json: the colours of the game's text (the colour ids the strings and the
// map messages' ~C markup select, by name) and the map message box's placement: where a fresh message's
// lines start, the y of each box position the ~J markup selects, and the "display all" glyph budget.
// Mods patch it with the data-merge rules; every key is checked.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "formats/common/status.h"

namespace sh::text {

// The colour ids in the game's order: the index is what the code and the markup pass.
inline constexpr std::size_t kTextColorCount = 8;
inline constexpr std::string_view kTextColorNames[kTextColorCount] = {
    "gold", "dark_grey", "green", "nuclear", "red", "green_alt", "light_grey", "white"};

// Message box positions of the ~J markup (0..4).
inline constexpr std::size_t kMessageBoxPositions = 5;
inline constexpr std::string_view kMessageBoxPositionNames[kMessageBoxPositions] = {
    "top", "bottom", "upper_middle", "lower", "grid"};

struct TextStyle {
    std::array<std::uint32_t, kTextColorCount> rgb{};  // 0x00BBGGRR
    std::int32_t message_color = 0;                      // colour id a fresh map message starts with
    std::int32_t message_line_start_x = 0;               // relative to the screen centre
    std::array<std::int32_t, kMessageBoxPositions> message_box_y{};  // first line y, relative to the centre
    std::int32_t display_all_length = 0;                 // glyph budget of a ~D (display all) page
};

Result<TextStyle> parse_text_style(std::string_view json_text);

// Base menu/text_style.json and every active mod's patch, in load order. `source` gets the file read.
Result<TextStyle> load_text_style(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                  std::string* source = nullptr);

}  // namespace sh::text
