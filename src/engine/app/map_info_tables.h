// SPDX-License-Identifier: GPL-3.0-or-later
//
// The running game's table of map types and its environment presets (src/engine/app/map_info_tables.cpp), filled
// at boot from gamedata/config/map_info.json. The records keep the game's layout while its world code reads them.
#pragma once

#include <cstdint>

#include "engine/game_state/session.h"

namespace sh::app {

// A speed zone (player speed) and a water zone (water look) of a map type, Q4 metres.
struct SpeedZoneRecord {
    std::int8_t kind;  // -1 ends the list
    std::int16_t min_x, max_x, min_z, max_z;
};
struct WaterZoneRecord {
    std::uint8_t enabled;
    std::int16_t illumination;
    std::int16_t min_x, max_x, min_z, max_z;
};

struct MapTypeRecord {
    std::int16_t scenery_file;  // disc file of the scenery models, -1 for none
    char tag[4];
    std::uint8_t flags;
    const WaterZoneRecord* water_zones;  // null for none
    const SpeedZoneRecord* speed_zones;
};

constexpr int kMapTypes = 16;
constexpr int kEnvironmentPresets = 21;

// Fills both tables once from gamedata/config/map_info.json and the mods' overrides of it.
void load_map_info(const char* gamedata_dir, const char* mods_dir);
// `mods reload` (between frames, no map drawn): the tables built again from the data.
void reload_map_info(const char* gamedata_dir, const char* mods_dir);

// The record of map type `type`; null outside the table.
const MapTypeRecord* map_type_record(int type);
// The kEnvironmentPresets environment presets.
sh::game::EnvironmentPreset* environment_presets();

}  // namespace sh::app
