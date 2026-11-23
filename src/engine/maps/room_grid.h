/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

/* The room under a world point, from a map's room grid: a rectangle of the world split into square
 * cells, one room number per cell, stored column by column (all the cells of one x column, z
 * ascending, then the next column). Shared by every map; a map describes its grid in
 * gamedata/maps/<map>/rooms.json and the cells come from the disc. */

#include <cstddef>
#include <cstdint>
#include <span>

namespace sh::maps {

struct RoomGrid {
    // Q12 world bounds: min inclusive, max exclusive.
    std::int32_t min_x = 0;
    std::int32_t max_x = 0;
    std::int32_t min_z = 0;
    std::int32_t max_z = 0;
    std::int32_t cell = 40 * 4096;  // the edge of one cell (the world chunk, 40 m)
    std::span<const std::uint8_t> cells;
    // A cell inside the grid that holds 0 reads as this room instead (0 keeps it 0).
    std::uint8_t empty_cell_room = 0;
};

// 0 outside the grid. The cell index truncates toward zero, as the game divides; a cell beyond the
// stored ones (a grid larger than its data) reads 0.
constexpr std::uint8_t room_grid_lookup(const RoomGrid& grid, std::int32_t x, std::int32_t z) {
    if (grid.cell <= 0 || x < grid.min_x || x >= grid.max_x || z < grid.min_z || z >= grid.max_z) {
        return 0;
    }
    const std::int32_t stride = (grid.max_z - grid.min_z) / grid.cell;
    const std::int32_t column = (x - grid.min_x) / grid.cell;
    const std::int32_t row = (z - grid.min_z) / grid.cell;
    const std::size_t index = static_cast<std::size_t>(column) * static_cast<std::size_t>(stride) +
                              static_cast<std::size_t>(row);
    const std::uint8_t room = index < grid.cells.size() ? grid.cells[index] : std::uint8_t{0};
    return room == 0 ? grid.empty_cell_room : room;
}

}  // namespace sh::maps
