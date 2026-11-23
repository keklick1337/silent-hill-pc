// SPDX-License-Identifier: GPL-3.0-or-later
// What dead enemies leave behind for good (config `persistent_remains`, a gameplay change that is off by default):
// the bodies and the blood pools under them, kept per map and room after the game
// itself has let the body go, put back when the place is entered again and written into the save.
//
// The store is plain data by name: a body is its character's id ("silenthill:Groaner"), where it lies and the pose
// its death left it in. The game side (entities/game/remains_host.cpp) fills it and stands the bodies back up as
// lifeless statues; this file knows nothing of the game.

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "engine/entities/character_record.h"
#include "formats/common/status.h"

namespace sh::entities {

inline constexpr const char* kRemainsFormat = "sh-remains/1";

// A body where it fell.
struct CorpseRemains {
    std::string chara;  // character id
    int room = 0;
    Vec3 position;
    Angles rotation;
    // The pose: the clip status, its time and keyframe as the animation left them.
    std::uint8_t clip_status = 0;
    std::int32_t clip_time = 0;
    std::int16_t keyframe = 0;
    std::uint8_t palette = 0;
    std::int16_t timer = 0;
    std::uint8_t variant = 0;
    bool operator==(const CorpseRemains&) const = default;
};

// A blood pool on the floor, fully spread.
struct BloodStain {
    int room = 0;
    Vec3 position;
    std::int32_t size = 0;
    std::uint8_t color = 0;  // blood colour slot
    std::uint8_t shape = 0;  // 0..3, the pool's outline
    bool operator==(const BloodStain&) const = default;
};

struct MapRemains {
    std::vector<CorpseRemains> corpses;  // oldest first
    std::vector<BloodStain> stains;
    bool operator==(const MapRemains&) const = default;
};

class RemainsStore {
public:
    // Per map; past them the oldest entry goes.
    static constexpr std::size_t kMaxCorpses = 96;
    static constexpr std::size_t kMaxStains = 256;
    // Two bodies of one character nearer than this (Q12 metres on each axis) are the same body seen twice.
    static constexpr std::int32_t kSameSpot = 1024;

    // Returns false when the entry replaced one already standing at that spot.
    bool add_corpse(std::string_view map, const CorpseRemains& corpse);
    bool add_stain(std::string_view map, const BloodStain& stain);

    const MapRemains* find(std::string_view map) const;
    const std::map<std::string, MapRemains, std::less<>>& maps() const { return maps_; }
    bool empty() const { return maps_.empty(); }
    void clear();
    // Grows with every change, so a holder of indices into a map's lists knows when they went stale.
    std::uint64_t revision() const { return revision_; }

    // JSON "sh-remains/1": maps in name order, entries oldest first; the same store gives the same text.
    std::string to_json() const;
    static Result<RemainsStore> from_json(std::string_view text);

    bool operator==(const RemainsStore& other) const { return maps_ == other.maps_; }

private:
    std::map<std::string, MapRemains, std::less<>> maps_;
    std::uint64_t revision_ = 0;
};

// Which stored bodies stand in the world: one comes in inside `show` and leaves past `hide` (Q12 metres on the
// ground plane, hide > show so a body at the edge does not flicker).
struct RemainsReach {
    std::int32_t show = 22 * 4096;
    std::int32_t hide = 26 * 4096;
};
bool remains_within(std::int32_t x, std::int32_t z, std::int32_t from_x, std::int32_t from_z, std::int32_t reach);

}  // namespace sh::entities
