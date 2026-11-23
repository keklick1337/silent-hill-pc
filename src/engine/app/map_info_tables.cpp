// SPDX-License-Identifier: GPL-3.0-or-later
//
// The map types and the environment presets of gamedata/config/map_info.json as the
// running game reads them: one table of map types (scenery file, tag, flags, water and speed zones) and the 21
// environment presets, filled once at boot from sh::gconfig::MapInfoLibrary. The zone lists keep an end entry
// because the world drawing walks them as runs. The record layout is the one the world code reads.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "engine/app/map_info_tables.h"
#include "engine/gconfig/map_info.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/log/sh_log.h"

namespace {

using sh::app::MapTypeRecord;
using sh::app::SpeedZoneRecord;
using sh::app::WaterZoneRecord;
constexpr int kMapTypes = sh::app::kMapTypes;

}  // namespace

namespace {

sh::game::EnvironmentPreset s_environments[sh::app::kEnvironmentPresets];
MapTypeRecord s_map_types[kMapTypes];

// Zone storage outlives the boot call: the map types point into it.
std::vector<std::vector<SpeedZoneRecord>> s_speed_sets;
std::vector<std::vector<WaterZoneRecord>> s_water_sets;

// The scenery models a map type names by stem: "THR" -> BG/THR_GLB.PLM.
std::int32_t scenery_file(const std::string& stem) {
    static const char* const kStems[] = {"THR", "SPR", "SPU", "RSR", "RSU", "APR", "APU", "DR", "DRU"};
    for (const char* known : kStems) {
        if (stem == known) {
            return sh::loader::disc_files().find(("BG/" + stem + "_GLB.PLM").c_str());
        }
    }
    return -1;
}

sh::game::Rgbx color(const std::array<std::uint8_t, 3>& rgb) { return {rgb[0], rgb[1], rgb[2], 0}; }

}  // namespace

namespace {
bool s_map_info_done = false;
}  // namespace

void sh::app::reload_map_info(const char* gamedataDir, const char* modsDir) {
    s_map_info_done = false;
    load_map_info(gamedataDir, modsDir);
}

void sh::app::load_map_info(const char* gamedataDir, const char* modsDir) {
    using namespace sh::gconfig;

    bool& s_done = s_map_info_done;
    if (s_done || gamedataDir == nullptr) {
        return;
    }

    std::vector<std::string> warnings;
    MapInfoLoadSource source;
    auto library = MapInfoLibrary::load(gamedataDir, modsDir ? modsDir : "", warnings, &source);
    if (!library) {
        SH_ERR("[MAPINFO] %s (map types and environments stay empty)", library.error().c_str());
        return;
    }
    for (const std::string& warning : warnings) {
        SH_WARN("[MAPINFO] %s", warning.c_str());
    }
    if (library.value().maps().size() != kMapTypes) {
        SH_ERR("[MAPINFO] map_info.json has %d map types, expected %d", (int)library.value().maps().size(), kMapTypes);
        return;
    }
    s_done = true;

    const MapInfoLibrary& lib = library.value();

    // Reserved first: the vectors must not move once pointers into them are stored.
    s_speed_sets.clear();
    s_water_sets.clear();
    s_speed_sets.reserve(lib.maps().size());
    s_water_sets.reserve(lib.maps().size());

    for (std::size_t i = 0; i < lib.maps().size(); i++) {
        const MapTypeData& src = lib.maps()[i];
        MapTypeRecord& dst = s_map_types[i];

        std::memset(&dst, 0, sizeof(dst));
        dst.scenery_file = static_cast<std::int16_t>(src.plm.empty() ? -1 : scenery_file(src.plm));
        std::strncpy(dst.tag, src.tag.c_str(), sizeof(dst.tag) - 1);
        dst.flags = src.flags;

        s_speed_sets.emplace_back();
        for (const SpeedZoneData& z : *lib.speed_zones(src.speed_zones)) {
            s_speed_sets.back().push_back({static_cast<std::int8_t>(z.kind), z.min_x, z.max_x, z.min_z, z.max_z});
        }
        s_speed_sets.back().push_back({-1, 0, 0, 0, 0});  // the end entry
        dst.speed_zones = s_speed_sets.back().data();

        if (!src.water_zones.empty()) {
            s_water_sets.emplace_back();
            for (const WaterZoneData& z : *lib.water_zones(src.water_zones)) {
                s_water_sets.back().push_back(
                    {static_cast<std::uint8_t>(z.enabled), z.illumination, z.min_x, z.max_x, z.min_z, z.max_z});
            }
            s_water_sets.back().push_back({});  // the end entry
            dst.water_zones = s_water_sets.back().data();
        }
    }

    for (std::size_t i = 0; i < MapInfoLibrary::kEnvironmentCount; i++) {
        const EnvPresetData& src = lib.environments()[i];
        sh::game::EnvironmentPreset& dst = s_environments[i];

        std::memset(&dst, 0, sizeof(dst));
        dst.flags = src.flags;
        dst.light_level_max = src.light_level;
        dst.light_mode = src.light_mode;
        dst.spot_light_intensity = src.alpha;
        dst.world_light_intensity = src.tint_intensity;
        dst.world_tint[0] = src.tint[0];
        dst.world_tint[1] = src.tint[1];
        dst.world_tint[2] = src.tint[2];
        dst.fog_mode = src.fog_mode;
        dst.fog_distance = src.fog_distance;
        dst.fog_color = color(src.fog_color);
        dst.tint_light_overlap = src.screen_tint_on;
        dst.point_light_tint = color(src.screen_tint_add);
        dst.world_tint_color = color(src.screen_tint_sub);
        dst.snow_color = color(src.particle_color_a);
        dst.rain_color = color(src.particle_color_b);
    }

    const std::string from = source.mod_id.empty() ? std::string() : " (mod " + source.mod_id + ")";
    SH_LOG("[MAPINFO] gamedata/config: %d map types, %d environments%s", (int)lib.maps().size(),
           (int)lib.environments().size(), from.c_str());
}

const sh::app::MapTypeRecord* sh::app::map_type_record(int type) {
    return type >= 0 && type < kMapTypes ? &s_map_types[type] : nullptr;
}

sh::game::EnvironmentPreset* sh::app::environment_presets() { return s_environments; }
