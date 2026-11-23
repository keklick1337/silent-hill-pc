// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The rules a map gives the player and the ground (gamedata/maps/<map>/overlay.json), so
// that nothing is decided by comparing the map's name. A map without the keys has none of them.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sh::maps {

struct MapRules {
    // "player_updates": functions of Harry's behaviour.lua the map runs every tick before his controller
    // (map2_s00: "padlock_update").
    std::vector<std::string> player_updates;
    // "pin_reactions": the map's reaction rows carry a Romper's pin of Harry; "floor_reactions": its rows carry the
    // floor reactions (map6_s04).
    bool pin_reactions = false;
    bool floor_reactions = false;
    // "floor_push": the floor pushes Harry (the push comes in the player's PushX / PushZ; map1_s05).
    bool floor_push = false;
    // "harry_anim_files": Harry's other keyframe files the map loads over its HB_M<n>S<nn>.ANM in a scene
    // (map6_s04: the boss fight's ANIM/HBM6_S4B.ANM). Its reaction rows past the map file's own keyframes play
    // keyframes of these; the reaction import takes them from here.
    std::vector<std::string> harry_anim_files;
    // "level_floor_rooms": rooms whose floor is level whatever the ground says (map1_s00 room 13).
    std::vector<int> level_floor_rooms;
    // "void_drop": where there is no ground the ground is a drop 4 m down. "no_ground": where the collision has no
    // ground ("no_ground_max_z": only up to that z, metres); "open_ground": on the open ground kind too.
    struct VoidDrop {
        bool no_ground = false;
        bool no_ground_bounded = false;
        std::int32_t no_ground_max_z = 0;  // Q12
        bool open_ground = false;
        bool operator==(const VoidDrop&) const = default;
    } void_drop;

    bool operator==(const MapRules&) const = default;
};

// The rules of an overlay.json text; problems (a key of the wrong type) go to `error` and the rest still apply.
MapRules parse_map_rules(std::string_view overlay_json, std::string* error = nullptr);

}  // namespace sh::maps
