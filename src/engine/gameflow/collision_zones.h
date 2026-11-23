// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Ground boxes that switch a character's collision trigger flags while it stands in them (the town has two:
// a spot where the map's object collisions give way to the alternative set). Before each NPC's behaviour runs
// the game asks for its flags; after the behaviour the flags of the frame are restored.
//
// Boxes are gamedata/config/collision_zones.json; a mod replaces the file whole.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

namespace sh::gameflow {

struct CollisionZone {
    std::vector<std::string> maps;      // map type tags ("THR"); the zone is live on any of them
    std::int32_t min_x = 0, max_x = 0;  // Q12 metres, inclusive
    std::int32_t min_z = 0, max_z = 0;
    std::uint16_t clear = 0;            // trigger flag bits dropped inside the box
    std::uint16_t set = 0;              // and the bits raised
};

class CollisionZones {
public:
    static CollisionZones builtin();
    static Result<CollisionZones> parse(std::string_view json);

    // The flags a character at (x, z) on the map tagged `map` collides with, starting from the frame's flags.
    std::uint16_t apply(std::string_view map, std::int32_t x, std::int32_t z, std::uint16_t flags) const;

    const std::vector<CollisionZone>& zones() const { return zones_; }

private:
    std::vector<CollisionZone> zones_;
};

}  // namespace sh::gameflow
