#pragma once

// Per-map camera rules as data (gamedata/config/camera.json "map_rules"
// section 8): the spots where the self view never engages, the map types whose doorways are
// narrow, the anchor fix of one shipped zone and the widescreen swing of one authored shot. The
// game only says where the player is (map type, map, room, position); these functions turn that
// into the neutral FrameInput fields the camera update reads.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "engine/camera/camera_types.h"

namespace sh::camera {

// A map type by its tag in gamedata/config/map_info.json. The tag list repeats "ER" (the second
// entry is the type of map7_*), so "ER#2" names that second entry alone; a bare tag matches every
// entry with it.
struct MapTypeSelector {
    std::string tag;
    int occurrence = 0;  // 1-based among the entries with that tag; 0 = any of them
};

// A box (Q12, inclusive edges) on maps of one type where the self view is not allowed.
struct SelfViewBlock {
    MapTypeSelector map_type;
    std::int32_t min_x = 0;
    std::int32_t max_x = 0;
    std::int32_t min_z = 0;
    std::int32_t max_z = 0;
};

// On one map and room, every zone whose road area is the single point (road_x, road_z) (Q4) has
// its anchor moved by shift_x (Q12).
struct RoadAnchorRule {
    std::string map;
    int room = 0;
    std::int16_t road_x = 0;
    std::int16_t road_z = 0;
    std::int32_t shift_x = 0;
};

// On one map and room, the fixed shot whose camera stands within `tolerance` of the anchor (Q12)
// swings its look-at by the angle the Hor+ widescreen frame adds to the 4:3 one.
struct WideShotRule {
    std::string map;
    int room = 0;
    std::int32_t anchor_x = 0;
    std::int32_t anchor_z = 0;
    std::int32_t tolerance = 0;
};

// The shipped rules are the defaults, so a camera.json without "map_rules" changes nothing.
struct MapCameraRules {
    std::vector<SelfViewBlock> self_view_blocks{
        {{"THR", 0}, 826572, 942080, -376832, -278528},  // x 201.8..230, z -92..-68
        {{"SPR", 0}, -942080, -823296, -49152, 49152},   // x -230..-201, z -12..12
    };
    std::vector<MapTypeSelector> tight_doorway_types{{"ER", 2}, {"HP", 0}, {"HU", 0}};
    std::vector<RoadAnchorRule> road_anchor_shifts{
        {"map3_s01", 10, 281, 955, 1024},  // the kitchen shot: road point (17.5625, 59.6875), +0.25 m
    };
    std::vector<WideShotRule> wide_shots{
        {"map2_s00", 23, -769024, 1243136, 4096},  // the alley: anchor (-187.75, 303.5), 1 m
    };
};

// Where the player is, as the game names it.
struct MapPlace {
    std::string_view map_type;     // map_info.json tag of the running map type; empty when none
    int map_type_occurrence = 0;   // 1-based among the map_info.json entries with that tag
    std::string_view map;          // "map3_s01"; empty when there is no save
    int room = -1;
};

// The Hor+ widescreen half widths of the frame (the renderer's numbers; valid = Hor+ is on).
struct HorPlusWidths {
    bool valid = false;
    float half_43 = 0.0f;
    float half_wide = 0.0f;
};

bool map_type_matches(const MapTypeSelector& selector, const MapPlace& place);

bool self_view_blocked(const MapCameraRules& rules, const MapPlace& place, std::int32_t x, std::int32_t z);
bool tight_doorways(const MapCameraRules& rules, const MapPlace& place);
// The anchor shifts for the zone list of the running map (indices into `roads`).
std::vector<RoadAnchorShift> road_anchor_shifts(const MapCameraRules& rules, const MapPlace& place,
                                                const std::vector<CameraRoad>& roads);
// The first wide-shot rule of the place, with its yaw delta for the frame widths and the
// projection distance; disabled when none applies or the frame is not wider than 4:3.
WideShot wide_shot(const MapCameraRules& rules, const MapPlace& place, const HorPlusWidths& widths,
                   std::int32_t geom_screen_dist);

}  // namespace sh::camera
