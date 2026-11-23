// SPDX-License-Identifier: GPL-3.0-or-later

#include "framework/sh_test.h"

#include "engine/world/cell_grid.h"

#include <vector>

using namespace sh::world;

SH_TEST(cell_grid_names_place_files) {
    CellGrid grid;
    SH_CHECK(grid.file_at({0, 0}) == CellGrid::kNone);
    grid.add_file("THR", "THR0205.IPD", 1100);
    grid.add_file("THR", "THRF908.IPD", 1101);
    grid.add_file("THR", "HP0001.IPD", 1200);   // other map
    grid.add_file("THR", "THRZZ01.IPD", 1300);  // not a cell name
    SH_CHECK(grid.file_at({2, 5}) == 1100);
    SH_CHECK(grid.file_at({-7, 8}) == 1101);
    SH_CHECK(grid.file_at({0, 1}) == CellGrid::kNone);
    SH_CHECK(grid.file_at({8, 0}) == CellGrid::kNone);  // outside
    grid.set({-1, 8}, 42);
    SH_CHECK(grid.file_at({-1, 8}) == 42);
    grid.set({20, 0}, 7);  // ignored
    grid.clear();
    SH_CHECK(grid.file_at({2, 5}) == CellGrid::kNone);
}

SH_TEST(cell_grid_digits) {
    SH_CHECK(parse_cell_digits('0', 'A') == 10);
    SH_CHECK(parse_cell_digits('F', 'F') == -1);
    SH_CHECK(parse_cell_digits('7', 'F') == 127);
    SH_CHECK(parse_cell_digits('8', '0') == -128);
    SH_CHECK(!parse_cell_digits('G', '0'));
    SH_CHECK(!parse_cell_digits('0', 'Z'));
}

SH_TEST(cell_grid_distances) {
    SH_CHECK((cell_of(0, 0) == CellCoord{0, 0}));
    SH_CHECK((cell_of(-1, kCellSize) == CellCoord{-1, 1}));
    SH_CHECK(distance_to_cell(100, 100, {0, 0}) == 0);
    SH_CHECK(distance_to_cell(-256, 0, {0, 0}) == 256);
    const std::int32_t diagonal = distance_to_cell(kCellSizeGeometry + 300, 400 + kCellSizeGeometry, {0, 0});
    SH_CHECK(diagonal >= 499 && diagonal <= 500);
    // Exterior windows: one metre band (Q8 16 m in Q12 scale) is free.
    SH_CHECK(padded_distance_to_cell(-4096 * 10, 0, {0, 0}, true) == 0);
    SH_CHECK(padded_distance_to_cell(-4096 * 10, 0, {0, 0}, false) == 2560);
}

SH_TEST(cell_grid_window_slot) {
    std::vector<WindowSlot> slots(3);
    for (WindowSlot& s : slots) s.occupied = true;
    SH_CHECK(!pick_window_slot(slots, false));
    slots[1].outside_count = 4;
    slots[2].outside_count = 9;
    SH_CHECK(pick_window_slot(slots, false) == 2u);
    slots[0].material_count = 3;
    slots[0].padded_distance = 10;
    slots[1].material_count = 3;
    slots[1].padded_distance = 20;
    slots[2].material_count = 5;  // the point is inside it: kept
    SH_CHECK(pick_window_slot(slots, true) == 1u);
    slots[0].occupied = false;
    SH_CHECK(pick_window_slot(slots, true) == 0u);
    SH_CHECK(next_outside_count(4, 1, 1) == 5u);
    SH_CHECK(next_outside_count(4, 0, 1) == 0u);
}

SH_TEST(cell_grid_collision_lookup) {
    CellGrid grid;
    grid.set({0, 0}, 10);
    grid.set({1, 0}, 11);
    std::vector<ResidentCell> slots = {{{1, 0}, true}, {{0, 0}, false}};
    CollisionCellLookup at = collision_cell_at(slots, grid, kCellSize + 5, 5);
    SH_CHECK(at.kind == CollisionCellLookup::Kind::Resident && at.slot == 0);
    SH_CHECK(collision_cell_at(slots, grid, 5, 5).kind == CollisionCellLookup::Kind::Missing);
    SH_CHECK(collision_cell_at(slots, grid, -5, 5).kind == CollisionCellLookup::Kind::Open);
    SH_CHECK(in_collision_scope({3, 3}, {0, 0}, false));
    SH_CHECK(!in_collision_scope({2, 0}, {0, 0}, true));
    SH_CHECK(in_collision_scope({-1, 1}, {0, 0}, true));
}

SH_TEST(cell_grid_outdoor_cells) {
    CellGrid grid;
    for (int x = 0; x < 4; x++) grid.set({x, 0}, 1);  // a street of four cells (room 1)
    grid.set({0, 2}, 2);                              // a house on one cell (room 2)
    grid.set({3, 2}, 3);                              // a parked cell inside the street room
    const RoomAt room = [](std::int32_t, std::int32_t z) -> std::uint8_t {
        return z < kCellSize ? 1 : (z < 3 * kCellSize ? 2 : 0);
    };
    OutdoorCells out;
    const std::vector<CellCoord> parked = {{3, 2}};
    out.classify(grid, parked, room);
    SH_CHECK(out.outdoor({0, 0}) && out.outdoor({3, 0}));
    SH_CHECK(!out.outdoor({3, 2}));
    SH_CHECK(out.room_cells(1) == 4);
    // Room 2 spans the house cell and the parked cell (two cells, never partial there): indoors.
    SH_CHECK(!out.outdoor({0, 2}));
    SH_CHECK(out.count() == 4);
    out.classify(grid, parked, {});
    SH_CHECK(out.count() == 0);
}
