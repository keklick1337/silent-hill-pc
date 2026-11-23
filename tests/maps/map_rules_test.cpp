// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The map rules of the shipped overlay.json files against a golden table of the original's rules by map name: the
// pin, floor-reaction, push-floor and level-floor maps, the map2_s00 hook and the drop maps.

#include "framework/sh_test.h"

#include "engine/maps/map_rules.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using sh::maps::MapRules;

SH_TEST(map_rules_match_the_old_map_names) {
    const std::set<std::string> pin = {"map2_s00", "map2_s02", "map4_s02", "map5_s01", "map6_s00", "map6_s02"};
    int maps = 0;
    for (const auto& dir : fs::directory_iterator(fs::path(SH_PROJECT_ROOT) / "gamedata" / "maps")) {
        const fs::path file = dir.path() / "overlay.json";
        if (!fs::is_regular_file(file)) {
            continue;
        }
        ++maps;
        const std::string map = dir.path().filename().string();
        std::ifstream in(file);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::string error;
        const MapRules r = sh::maps::parse_map_rules(text, &error);
        SH_CHECK(error.empty());
        SH_CHECK(r.pin_reactions == (pin.count(map) != 0));
        SH_CHECK(r.floor_reactions == (map == "map6_s04"));
        SH_CHECK(r.floor_push == (map == "map1_s05"));
        SH_CHECK(r.level_floor_rooms == (map == "map1_s00" ? std::vector<int>{13} : std::vector<int>{}));
        SH_CHECK(r.player_updates ==
                 (map == "map2_s00" ? std::vector<std::string>{"padlock_update"} : std::vector<std::string>{}));
        SH_CHECK(r.harry_anim_files ==
                 (map == "map6_s04" ? std::vector<std::string>{"ANIM/HBM6_S4B.ANM"} : std::vector<std::string>{}));
        MapRules::VoidDrop drop;
        if (map == "map5_s01") {
            drop = {true, true, 0, false};  // no ground with z <= 0
        } else if (map == "map6_s00") {
            drop = {true, false, 0, true};  // no ground anywhere, and the open ground
        }
        SH_CHECK(r.void_drop == drop);
    }
    SH_CHECK(maps == 43);
}

SH_TEST(map_rules_report_bad_keys) {
    std::string error;
    const MapRules r = sh::maps::parse_map_rules(R"json({"floor_push": 1, "void_drop": {"no_ground_max_z": -1.5}})json",
                                                 &error);
    SH_CHECK(!r.floor_push && !error.empty());
    SH_CHECK(r.void_drop.no_ground_bounded && r.void_drop.no_ground_max_z == -6144);
}
