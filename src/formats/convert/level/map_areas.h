#pragma once

#include <span>
#include <string_view>

namespace sh::convert::level {

// The 43 maps of the game and the area (set of IPD cells) each one draws. Taken from the map headers
// (`mapInfo` of each map) and its MAP_INFOS table. Several maps share an area.
struct MapArea {
    std::string_view map;   // "map0_s00"
    std::string_view area;  // IPD name prefix: "THR" for BG/THR0000.IPD
};

std::span<const MapArea> map_areas();

// "BG/THR0000.IPD" or "THR0000" -> "THR": the name without the four hex digits of the cell. Empty for other names.
std::string_view area_of_ipd(std::string_view game_path_or_name);

// The area's global model library ("BG/THR_GLB.PLM"), empty for areas without one (interiors).
std::string_view global_plm_of_area(std::string_view area);

}  // namespace sh::convert::level
