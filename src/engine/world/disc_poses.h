#pragma once

// Placements of world objects that live on the disc (world_objects.json "pose"): a 20-byte
// record in the map's overlay file (s32 x, y, z at 0/4/8, s16 rx, ry, rz at 12/14/16, little endian). Where the
// overlay's tables lie is the map's table registry, gamedata/maps/<map>/tables.json: the
// overlay file under gamedata/raw/, the address the file is loaded at, and the tables with their disc addresses.
// No game types: the caller reads the two files.

#include "engine/world/world_objects.h"
#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::world {

struct OverlayTable {
    std::string name;        // the registry's "symbol"
    std::uint32_t address = 0;
    std::uint32_t bytes = 0;  // bytes copied from the disc; 0 for tables that are not flat copies
};

struct OverlayTableRegistry {
    std::string bin;            // overlay file, relative to gamedata/raw/
    std::uint32_t base = 0;     // disc address of the file's first byte
    std::vector<OverlayTable> tables;
};

Result<OverlayTableRegistry> parse_overlay_tables(std::string_view json_text);

struct DiscPose {
    std::array<std::int32_t, 3> position{};
    std::array<std::int16_t, 3> rotation{};
};

inline constexpr std::uint32_t kDiscPoseBytes = 20;

// Row `ref.index` of the table "g_<disc_table>" of the registry (or of the table starting at ref.disc_address when
// the registry has none of that name). The row must lie inside a flat table of the registry, as the game only has
// those in memory, and inside the overlay file.
Result<DiscPose> resolve_disc_pose(const OverlayTableRegistry& registry, const WorldObjectPoseRef& ref,
                                   std::span<const std::uint8_t> overlay);

}  // namespace sh::world
