// SPDX-License-Identifier: GPL-3.0-or-later
//
// The map type the world is drawn and streamed for (docs/RENDER.md section 6): taken from the running
// map when the player's cells are first placed on entering it, and kept until the next map does the same. Between
// a map's load and that moment the previous map's type stays current, as the game's world code always saw it.
#pragma once

#include <cstdint>
#include <string_view>

#include "engine/app/map_info_tables.h"

namespace sh::world {

// Speed zone kinds (gamedata/config/map_info.json "speed_zones"): the player's top speed in that rectangle.
inline constexpr int kSpeedZoneSlow = 0;
inline constexpr int kSpeedZoneNormal = 1;

class WorldMap {
public:
    // Enters the running map: its type becomes current, the cell stream gets the map's files and as many resident
    // slots as the map shows at once, and the cells around (x, z) start loading. Q12 metres.
    void enter(std::int32_t x, std::int32_t z);

    const sh::app::MapTypeRecord* info() const { return info_; }
    int map_type() const;  // -1 before the first map
    std::string_view tag() const;
    std::uint8_t flags() const { return info_ != nullptr ? info_->flags : 0; }
    bool is_town() const { return map_type() == 0; }
    const void* water_zones() const { return info_ != nullptr ? info_->water_zones : nullptr; }

    // The fastest speed zone over (x, z) (Q12 metres); slow where none covers it, normal on the opening street.
    int speed_zone(std::int32_t x, std::int32_t z) const;

    // The map's collision triggers the area queries walk (set by the map's event set-up).
    void* collision_triggers() const { return collision_triggers_; }
    void set_collision_triggers(void* triggers) { collision_triggers_ = triggers; }

private:
    const sh::app::MapTypeRecord* info_ = nullptr;
    void* collision_triggers_ = nullptr;
};

WorldMap& world_map();

}  // namespace sh::world
