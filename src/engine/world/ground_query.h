#pragma once

// Ground under a point: height, slope and material of the floor a character standing at (x, z) is on. Follows the
// behaviour of the original game; the results are the same integers.

#include "engine/world/cell_collision.h"

#include <cstdint>
#include <span>

namespace sh::world {

// A cell where the game placed it: the origin is the cell's own one moved by whole cells.
struct PlacedCell {
    const CellCollision* cell = nullptr;
    std::int32_t origin_x = 0;  // Q.8
    std::int32_t origin_z = 0;
};

// Every collision pass over a cell stamps the edges and pillars it has looked at, so that one pass looks at each once
// although several grid squares list it. The stamps outlive the pass: character movement raises some of them to
// skip a blocking edge in its next passes, and every pass sees those. The ledger is therefore shared by all queries
// on a placed cell.
struct VisitLedger {
    std::uint8_t* pass = nullptr;
    std::span<std::uint8_t, 256> marks;
};

// A placed cell with its ledger: what the swept queries (rays, movement) walk.
struct ActiveCell {
    PlacedCell placed;
    VisitLedger ledger;
};

struct GroundSample {
    std::int32_t height = 0;      // Q.12, world units; 8.0 when there is no ground
    std::int16_t slope_x = 0;     // Q.12 rise per unit along x
    std::int16_t slope_z = 0;
    std::int8_t ground_type = 0;  // 0 when there is no ground
};

// The ground at (x, z) (Q.12 world units). `enabled_groups` is the active collision group mask: an edge, floor or
// pillar whose group bit is clear is ignored. With no cell (nothing loaded there) the result is "no ground, 8 units
// down". The ledger is updated exactly as the original pass updates it.
GroundSample sample_ground(const PlacedCell* placed, VisitLedger ledger, std::int32_t x, std::int32_t z,
                           std::uint16_t enabled_groups);

// What sample_ground gives without a cell.
GroundSample no_ground();

}  // namespace sh::world
