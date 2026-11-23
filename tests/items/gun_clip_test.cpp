// SPDX-License-Identifier: GPL-3.0-or-later
// Clip capacity by weapon number from the shipped weapons/ data against the original's clip table
// (15 handgun rounds, 6 rifle and shotgun shells, Hyper Blaster unlimited, 0 for everything else).

#include "framework/sh_test.h"

#include "engine/items/item_library.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using sh::items::ItemLibrary;

namespace {

std::uint8_t original_clip(int weapon_number)
{
    switch (weapon_number) {
    case 32: return 15;
    case 33: case 34: return 6;
    case 35: return 0xFF;
    default: return 0;
    }
}

}  // namespace

SH_TEST(gamedata_gun_clips_match_original_table) {
    std::vector<std::string> warnings;
    const auto library = ItemLibrary::load(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata", warnings);
    SH_CHECK(library);
    for (int number = -1; number < 256; number++)
        SH_CHECK(library.value().clip_capacity(number) == original_clip(number));
    SH_CHECK(ItemLibrary::kUnlimitedClip == 0xFF);
}
