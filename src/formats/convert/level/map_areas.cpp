#include "formats/convert/level/map_areas.h"

#include <array>

namespace sh::convert::level {
namespace {

// ER2 (MapType 10) uses the same IPD files as ER: its MAP_INFOS tag is "ER".
constexpr std::array<MapArea, 43> kMaps = {{
    {"map0_s00", "THR"}, {"map0_s01", "THR"}, {"map0_s02", "ER"},  {"map1_s00", "SC"},  {"map1_s01", "SC"},
    {"map1_s02", "SU"},  {"map1_s03", "SU"},  {"map1_s04", "SU"},  {"map1_s05", "SU"},  {"map1_s06", "SC"},
    {"map2_s00", "THR"}, {"map2_s01", "ER"},  {"map2_s02", "SPR"}, {"map2_s03", "THR"}, {"map2_s04", "ER"},
    {"map3_s00", "HP"},  {"map3_s01", "HP"},  {"map3_s02", "HU"},  {"map3_s03", "HU"},  {"map3_s04", "HU"},
    {"map3_s05", "HU"},  {"map3_s06", "HP"},  {"map4_s00", "SPR"}, {"map4_s01", "ER"},  {"map4_s02", "SPU"},
    {"map4_s03", "SPU"}, {"map4_s04", "HU"},  {"map4_s05", "SPU"}, {"map4_s06", "SPR"}, {"map5_s00", "DR"},
    {"map5_s01", "RSR"}, {"map5_s02", "ER"},  {"map5_s03", "ER"},  {"map6_s00", "RSU"}, {"map6_s01", "ER"},
    {"map6_s02", "RSU"}, {"map6_s03", "DRU"}, {"map6_s04", "APU"}, {"map6_s05", "APU"}, {"map7_s00", "ER"},
    {"map7_s01", "ER"},  {"map7_s02", "ER"},  {"map7_s03", "ER"},
}};

struct GlobalPlm {
    std::string_view area;
    std::string_view path;
};

// FILE_BG_*_GLB_PLM of MAP_INFOS; THR, SPR, SPU, RSR, RSU, APR, APU, DR and DRU have one.
constexpr std::array<GlobalPlm, 9> kGlobals = {{
    {"THR", "BG/THR_GLB.PLM"},
    {"SPR", "BG/SPR_GLB.PLM"},
    {"SPU", "BG/SPU_GLB.PLM"},
    {"RSR", "BG/RSR_GLB.PLM"},
    {"RSU", "BG/RSU_GLB.PLM"},
    {"APR", "BG/APR_GLB.PLM"},
    {"APU", "BG/APU_GLB.PLM"},
    {"DR", "BG/DR_GLB.PLM"},
    {"DRU", "BG/DRU_GLB.PLM"},
}};

}  // namespace

std::span<const MapArea> map_areas() { return kMaps; }

std::string_view area_of_ipd(std::string_view game_path_or_name) {
    const auto slash = game_path_or_name.rfind('/');
    if (slash != std::string_view::npos) {
        game_path_or_name.remove_prefix(slash + 1);
    }
    const auto dot = game_path_or_name.rfind('.');
    if (dot != std::string_view::npos) {
        game_path_or_name = game_path_or_name.substr(0, dot);
    }
    if (game_path_or_name.size() <= 4) {
        return {};
    }
    return game_path_or_name.substr(0, game_path_or_name.size() - 4);
}

std::string_view global_plm_of_area(std::string_view area) {
    for (const GlobalPlm& g : kGlobals) {
        if (g.area == area) {
            return g.path;
        }
    }
    return {};
}

}  // namespace sh::convert::level
