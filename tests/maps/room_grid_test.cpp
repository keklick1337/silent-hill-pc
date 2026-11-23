/* The shared room grid (engine/maps/room_grid.h): bounds min-inclusive / max-exclusive, cells
 * column by column, truncating cell index, the empty-cell room and a grid larger than its data. */
#include "engine/maps/room_grid.h"
#include "framework/sh_test.h"

#include <array>
#include <cstdint>

namespace {

constexpr std::int32_t kM = 4096;  // one metre in Q12

// 2 columns (x -40..40 m) by 3 rows (z 0..120 m); cell = column * 3 + row.
constexpr std::array<std::uint8_t, 6> kCells = {1, 2, 0, 4, 5, 6};

constexpr sh::maps::RoomGrid grid(std::uint8_t empty = 0) {
    sh::maps::RoomGrid g;
    g.min_x = -40 * kM;
    g.max_x = 40 * kM;
    g.min_z = 0;
    g.max_z = 120 * kM;
    g.cells = kCells;
    g.empty_cell_room = empty;
    return g;
}

}  // namespace

SH_TEST(room_grid_cells_column_major) {
    SH_CHECK(sh::maps::room_grid_lookup(grid(), -40 * kM, 0) == 1);
    SH_CHECK(sh::maps::room_grid_lookup(grid(), -1, 40 * kM) == 2);
    SH_CHECK(sh::maps::room_grid_lookup(grid(), 0, 0) == 4);
    SH_CHECK(sh::maps::room_grid_lookup(grid(), 39 * kM, 119 * kM) == 6);
}

SH_TEST(room_grid_bounds_min_inclusive_max_exclusive) {
    SH_CHECK(sh::maps::room_grid_lookup(grid(), -40 * kM - 1, 0) == 0);
    SH_CHECK(sh::maps::room_grid_lookup(grid(), 40 * kM, 0) == 0);
    SH_CHECK(sh::maps::room_grid_lookup(grid(), 0, -1) == 0);
    SH_CHECK(sh::maps::room_grid_lookup(grid(), 0, 120 * kM) == 0);
    SH_CHECK(sh::maps::room_grid_lookup(grid(), 40 * kM - 1, 120 * kM - 1) == 6);
}

SH_TEST(room_grid_empty_cell_room_only_inside) {
    SH_CHECK(sh::maps::room_grid_lookup(grid(), -1, 80 * kM) == 0);
    SH_CHECK(sh::maps::room_grid_lookup(grid(26), -1, 80 * kM) == 26);
    SH_CHECK(sh::maps::room_grid_lookup(grid(26), 0, -1) == 0);
}

SH_TEST(room_grid_missing_cells_read_zero) {
    sh::maps::RoomGrid g = grid();
    g.cells = std::span<const std::uint8_t>(kCells.data(), 4);
    SH_CHECK(sh::maps::room_grid_lookup(g, 0, 40 * kM) == 0);
    SH_CHECK(sh::maps::room_grid_lookup(g, 0, 0) == 4);
}

static_assert(sh::maps::room_grid_lookup(grid(), 0, 0) == 4);
