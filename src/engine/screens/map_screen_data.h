// SPDX-License-Identifier: GPL-3.0-or-later
// The paper map screen as data: gamedata/menu/map_screen.json (zoom and pan, the player arrow,
// the page arrows, page links, the disc files of each page, where the player arrow stands on each
// page, player anchors of the school and hospital pages, the marking atlases and the marks of each page). Every mod's menu/map_screen.json patches it (engine/mods/data_merge.h).
// When the file is missing or refused the scalars keep the built-in defaults below and the
// tables stay empty (no links, no anchors, no marks).
#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "formats/common/status.h"

namespace sh::screens {

struct MapRgb {
    int r = 0, g = 0, b = 0;
    bool operator==(const MapRgb&) const = default;
};

struct MapPlayerArrow {
    int tip = 6;         // cells from the player to the tip
    int back = 6;        // cells from the player back to the base corners
    int half_width = 4;  // cells from the axis to each base corner
    MapRgb outline{16, 16, 16};
    MapRgb tip_color{0, 0, 255};
    MapRgb back_color{0, 255, 0};
};

struct MapPageArrows {
    int tip_y = 216;
    int base_y = 200;
    int half_width = 8;
    MapRgb tip_color{16, 16, 196};
    MapRgb base_color{128, 128, 196};
};

struct MapPageLink {
    std::string page;  // paper map name (paper_maps.json)
    std::string up;    // empty = none
    std::string down;  // empty = none
};

struct MapAnchorGrid {
    std::string name;
    int first_x = 0, first_z = 0;
    int size_x = 0, size_z = 0;
    std::vector<std::array<int, 2>> cells;  // [x - first_x][z - first_z], row-major in x

    // Map point of world cell (x, z); nullopt outside the grid.
    std::optional<std::array<int, 2>> at(int x, int z) const;
};

struct MapSprite {
    int u = 0, v = 0, w = 0, h = 0;
};

struct MapMarkingAtlas {
    std::string name;
    std::vector<MapSprite> sprites;  // sprite number n (from 1) is sprites[n - 1]
};

struct MapMark {
    int x = 0, y = 0;                         // half map pixels from the centre
    std::array<int, 2> sprites{0, 0};         // per half; 0 = none
    std::array<std::string, 2> flags;         // per half: the event flag that shows it
};

struct MapPageMarks {
    std::string page;
    std::string atlas;
    std::vector<MapMark> marks;
};

// Disc files of a page: the picture and the marking texture (empty = none).
struct MapPageFiles {
    std::string page;
    std::string image;
    std::string markings;
};

// One coordinate of a placement: a fixed map point, or the player's position on one axis (negated, plus `add`
// metres) divided by 2^shift toward zero, plus `offset`.
struct MapPlacementCoord {
    bool from_position = false;
    int fixed = 0;
    char axis = 'x';  // 'x' or 'z'
    bool negate = false;
    int add_metres = 0;
    int shift = 12;
    int offset = 0;
};

// The arrow placed from an anchor grid: anchor of the cell plus the position inside the cell (from its centre)
// times mul / div, per axis.
struct MapPlacementAnchor {
    std::string grid;
    std::array<int, 2> x{1, 1};  // mul, div
    std::array<int, 2> z{1, 1};
};

struct MapPlacementRule {
    std::vector<std::string> maps;            // empty = any map
    std::vector<std::array<int, 2>> cells;    // empty = any cell
    std::vector<std::array<int, 2>> chunks;   // empty = any chunk
    std::optional<int> cell_z_below;
    std::optional<std::array<int, 2>> cell_x_in;
    std::optional<MapPlacementAnchor> anchor;  // else x and z
    MapPlacementCoord x, z;
    int turn_degrees = 0;
};

struct MapPlacementGroup {
    std::vector<std::string> pages;
    std::vector<MapPlacementRule> rules;
};

struct MapScreenData {
    int zoom_step = 196;
    std::array<int, 2> focus_offset{80, 60};
    std::array<int, 2> pan_max{160, 120};
    int cursor_divisor = 75;
    int image_shade = 128;
    int marking_shade = 128;
    MapPlayerArrow player_arrow;
    MapPageArrows page_arrows;
    std::vector<MapPageLink> links;
    std::vector<MapAnchorGrid> anchors;
    std::vector<MapMarkingAtlas> atlases;
    std::vector<MapPageMarks> markings;
    std::vector<MapPageFiles> page_files;
    std::vector<MapPlacementGroup> placement;

    const MapAnchorGrid* anchor_grid(std::string_view name) const;
    const MapMarkingAtlas* atlas(std::string_view name) const;
};

Result<MapScreenData> parse_map_screen(std::string_view json_text);
Result<MapScreenData> load_map_screen(const std::filesystem::path& gamedata_dir,
                                      const std::filesystem::path& mods_dir, std::string* source);

}  // namespace sh::screens
