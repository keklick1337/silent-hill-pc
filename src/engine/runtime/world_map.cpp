// SPDX-License-Identifier: GPL-3.0-or-later
// The map type of the world drawing and streaming -- see world_map.h.

#include "engine/runtime/world_map.h"

#include <cstring>

#include "engine/game_state/save_record.h"
#include "engine/gconfig/map_info.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/maps/map_runtime.h"
#include "engine/runtime/cell_stream_runtime.h"

namespace sh::world {

namespace {

// Resident cell slots of a map. The game kept 1, 2 or 4; the wide view sees the cells next to the player's at the
// screen edges, so at least 4 stay resident, and an interior keeps the whole window the drawer shows (+-2 cells in
// X, +-1 in Z: 15) so moving between rooms never evicts the room being entered.
int slot_count(std::uint8_t flags) {
    if (flags & sh::gconfig::kMapInterior) {
        return 16;
    }
    int count = 4;
    if (flags & sh::gconfig::kMapOneActiveChunk) {
        count = 1;
    } else if (flags & sh::gconfig::kMapTwoActiveChunks) {
        count = 2;
    }
    return count < 4 ? 4 : count;
}

}  // namespace

void WorldMap::enter(std::int32_t x, std::int32_t z) {
    // The whole-town parked cells are recorded again by this map's placements.
    stream::clear_parked_cells();

    info_ = sh::app::map_type_record(sh::maps::map_runtime().map_type());
    if (info_ == nullptr) {
        return;
    }
    const std::uint8_t map_flags = info_->flags;
    stream::set_map_files(info_->tag, info_->scenery_file, slot_count(map_flags),
                          (map_flags & sh::gconfig::kMapInterior) == 0);
    if (is_town()) {
        // The town's lake shore cell is parked outside the grid.
        stream::place_file_at(sh::loader::disc_files().find("BG/THR05FD.IPD"), -1, 8);
    }
    stream::update(x, z, x, z);
}

int WorldMap::map_type() const {
    const sh::app::MapTypeRecord* first = sh::app::map_type_record(0);
    return info_ != nullptr ? static_cast<int>(info_ - first) : -1;
}

std::string_view WorldMap::tag() const {
    return info_ != nullptr ? std::string_view(info_->tag, strnlen(info_->tag, sizeof(info_->tag))) : std::string_view();
}

int WorldMap::speed_zone(std::int32_t x, std::int32_t z) const {
    if (sh::game::save_record().map == 0) {
        return kSpeedZoneNormal;
    }
    int kind = kSpeedZoneSlow;
    if (info_ == nullptr || info_->speed_zones == nullptr) {
        return kind;
    }
    // Zone bounds are Q4 metres.
    for (const sh::app::SpeedZoneRecord* zone = info_->speed_zones; zone->kind != -1; ++zone) {
        if (x >= zone->min_x * 256 && zone->max_x * 256 >= x && z >= zone->min_z * 256 && zone->max_z * 256 >= z &&
            kind < zone->kind) {
            kind = zone->kind;
        }
    }
    return kind;
}

WorldMap& world_map() {
    static WorldMap map;
    return map;
}

}  // namespace sh::world
