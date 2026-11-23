// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Map types and their environment presets as data: gamedata/config/map_info.json.
// Each map type has a tag, a scenery file, active-chunk flags and two named zone sets (player speed
// zones, water light zones); the environments are the presets the fog/light/tint blend reads.
// Fixed-point values are raw integers (Q4 zones, Q12 environment numbers). Mods patch the file in load order
// (engine/mods/data_merge.h).

#pragma once

#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sh::gconfig {

enum class SpeedZoneKind : std::int8_t { Slow = 0, Normal = 1, Fast = 2 };

struct SpeedZoneData {
    SpeedZoneKind kind = SpeedZoneKind::Normal;
    std::int16_t min_x = 0, max_x = 0, min_z = 0, max_z = 0;  // Q4
};

struct WaterZoneData {
    bool enabled = false;
    std::int16_t illumination = 0;
    std::int16_t min_x = 0, max_x = 0, min_z = 0, max_z = 0;  // Q4
};

// Bits of MapTypeData::flags.
constexpr std::uint8_t kMapOneActiveChunk = 1u << 0;
constexpr std::uint8_t kMapTwoActiveChunks = 1u << 1;
constexpr std::uint8_t kMapInterior = 1u << 2;
constexpr std::uint8_t kMapUnused3 = 1u << 3;

struct MapTypeData {
    std::string tag;
    std::string plm;  // scenery file name stem ("THR" -> BG/THR_GLB.PLM); empty for interiors
    std::uint8_t flags = 0;
    std::string speed_zones;  // key into MapInfoLibrary::speed_zones
    std::string water_zones;  // empty = none
};

struct EnvPresetData {
    std::uint8_t flags = 0, light_level = 0, light_mode = 0, unused_byte = 0;
    std::int16_t alpha = 0, tint_intensity = 0;
    std::array<std::int16_t, 3> tint{};
    std::uint8_t fog_mode = 0;
    std::int32_t fog_distance = 0;
    std::array<std::uint8_t, 3> fog_color{}, screen_tint_add{}, screen_tint_sub{}, particle_color_a{}, particle_color_b{};
    std::uint8_t screen_tint_on = 0;
};

struct MapInfoLoadSource {
    std::string mod_id;
};

class MapInfoLibrary {
public:
    static constexpr std::size_t kEnvironmentCount = 21;

    static Result<MapInfoLibrary> load(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                       std::vector<std::string>& warnings, MapInfoLoadSource* source = nullptr);
    static Result<MapInfoLibrary> parse(std::string_view json);

    const std::vector<MapTypeData>& maps() const { return maps_; }
    const std::vector<SpeedZoneData>* speed_zones(const std::string& name) const;
    const std::vector<WaterZoneData>* water_zones(const std::string& name) const;
    const std::vector<EnvPresetData>& environments() const { return environments_; }

private:
    std::vector<MapTypeData> maps_;
    std::map<std::string, std::vector<SpeedZoneData>> speed_;
    std::map<std::string, std::vector<WaterZoneData>> water_;
    std::vector<EnvPresetData> environments_;
};

}  // namespace sh::gconfig
