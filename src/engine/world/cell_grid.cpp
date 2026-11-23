// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/world/cell_grid.h"

#include "engine/world/fixed_trig.h"

namespace sh::world {

namespace {

std::int32_t floor_div(std::int32_t value, std::int32_t step) {
    return value >= 0 ? value / step : (value - (step - 1)) / step;
}

// Distance of `value` outside [min, max).
std::int32_t outside(std::int32_t value, std::int32_t min, std::int32_t max) {
    if (value < min)
        return min - value;
    return max <= value ? value - max : 0;
}

// One name digit as a byte: the parser subtracts '0' in a signed byte, so anything below '0' passes as a digit; letters
// pass when their signed distance from 'A' is at most 5, which also lets ':' to '@' through.
std::optional<std::uint8_t> digit_byte(char c) {
    const int code = static_cast<unsigned char>(c);
    if (static_cast<std::int8_t>(code - '0') < 10)
        return static_cast<std::uint8_t>(code - '0');
    if (static_cast<std::int8_t>(code - 'A') > 5)
        return std::nullopt;
    return static_cast<std::uint8_t>(code - 'A' + 10);
}

}  // namespace

CellCoord cell_of(std::int32_t x, std::int32_t z) {
    return {floor_div(x >> 4, kCellSizeGeometry), floor_div(z >> 4, kCellSizeGeometry)};
}

std::int32_t distance_to_cell(std::int32_t x_q8, std::int32_t z_q8, CellCoord cell) {
    const std::int32_t min_x = cell.x * kCellSizeGeometry;
    const std::int32_t min_z = cell.z * kCellSizeGeometry;
    return fixed_length3(outside(x_q8, min_x, min_x + kCellSizeGeometry), 0,
                         outside(z_q8, min_z, min_z + kCellSizeGeometry));
}

std::int32_t padded_distance_to_cell(std::int32_t x, std::int32_t z, CellCoord cell, bool exterior) {
    std::int32_t distance = distance_to_cell(x >> 4, z >> 4, cell);
    if (exterior) {
        distance -= 4096;
        if (distance < 0)
            distance = 0;
    }
    return distance;
}

std::optional<std::int32_t> parse_cell_digits(char high, char low) {
    const std::optional<std::uint8_t> hi = digit_byte(high);
    if (!hi)
        return std::nullopt;
    const std::optional<std::uint8_t> lo = digit_byte(low);
    if (!lo)
        return std::nullopt;
    return static_cast<std::int8_t>(static_cast<std::uint8_t>((*hi << 4) | *lo));
}

// ---- CellGrid ----------------------------------------------------------------------------------------------------

void CellGrid::clear() {
    files_ = empty_files();
}

bool CellGrid::contains(CellCoord cell) {
    return cell.x >= kMinX && cell.x < kEndX && cell.z >= kMinZ && cell.z < kEndZ;
}

void CellGrid::add_file(std::string_view prefix, std::string_view name, std::int32_t file) {
    if (name.size() < prefix.size() + 4 || name.substr(0, prefix.size()) != prefix)
        return;
    const std::optional<std::int32_t> x = parse_cell_digits(name[prefix.size()], name[prefix.size() + 1]);
    if (!x)
        return;
    const std::optional<std::int32_t> z = parse_cell_digits(name[prefix.size() + 2], name[prefix.size() + 3]);
    if (!z)
        return;
    set({*x, *z}, file);
}

std::int32_t CellGrid::file_at(CellCoord cell) const {
    return contains(cell) ? files_[index(cell)] : kNone;
}

void CellGrid::set(CellCoord cell, std::int32_t file) {
    if (contains(cell))
        files_[index(cell)] = static_cast<std::int16_t>(file);
}

// ---- Window ------------------------------------------------------------------------------------------------------

std::optional<std::size_t> pick_window_slot(std::span<const WindowSlot> slots, bool exterior) {
    std::optional<std::size_t> pick;
    std::uint32_t longest_outside = 0;
    std::int32_t most_materials = 0;
    std::int32_t farthest = 0;
    for (std::size_t i = 0; i < slots.size(); i++) {
        const WindowSlot& slot = slots[i];
        if (!slot.occupied)
            return i;
        if (!exterior) {
            if (longest_outside < slot.outside_count) {
                longest_outside = slot.outside_count;
                pick = i;
            }
            continue;
        }
        if (slot.padded_distance == 0)
            continue;
        if (most_materials < slot.material_count ||
            (slot.material_count == most_materials && farthest < slot.padded_distance)) {
            farthest = slot.padded_distance;
            most_materials = slot.material_count;
            pick = i;
        }
    }
    return pick;
}

// ---- Collision cells ---------------------------------------------------------------------------------------------

CollisionCellLookup collision_cell_at(std::span<const ResidentCell> slots, const CellGrid& grid, std::int32_t x,
                                      std::int32_t z) {
    const CellCoord cell = cell_of(x, z);
    for (std::size_t i = 0; i < slots.size(); i++) {
        if (slots[i].ready && slots[i].cell == cell)
            return {CollisionCellLookup::Kind::Resident, i};
    }
    if (grid.file_at(cell) != CellGrid::kNone)
        return {CollisionCellLookup::Kind::Missing, 0};
    return {CollisionCellLookup::Kind::Open, 0};
}

// ---- Outdoor cells -----------------------------------------------------------------------------------------------

namespace {

constexpr std::int32_t kMinRoomCells = 3;
constexpr std::int32_t kInset = 14 * 4096;
constexpr std::int32_t kSamples[5][2] = {
    {0, 0}, {-kInset, -kInset}, {-kInset, kInset}, {kInset, -kInset}, {kInset, kInset},
};
constexpr std::int32_t kGridWidth = CellGrid::kEndX - CellGrid::kMinX;

std::size_t cell_index(CellCoord cell) {
    return static_cast<std::size_t>((cell.z - CellGrid::kMinZ) * kGridWidth + (cell.x - CellGrid::kMinX));
}

}  // namespace

void OutdoorCells::classify(const CellGrid& grid, std::span<const CellCoord> parked, const RoomAt& room_at) {
    outdoor_.fill(0);
    room_cells_.fill(0);
    partial_.fill(0);
    count_ = 0;
    if (!room_at)
        return;

    std::array<std::array<std::uint8_t, 5>, kCellCount> sampled{};
    grid.for_each_cell([&](CellCoord cell) {
        std::array<std::uint8_t, 5>& rooms = sampled[cell_index(cell)];
        const std::int32_t centre_x = cell.x * kCellSize + kCellSize / 2;
        const std::int32_t centre_z = cell.z * kCellSize + kCellSize / 2;
        for (std::size_t s = 0; s < 5; s++) {
            rooms[s] = room_at(centre_x + kSamples[s][0], centre_z + kSamples[s][1]);
            bool seen = false;
            for (std::size_t k = 0; k < s && !seen; k++)
                seen = rooms[k] == rooms[s];
            if (!seen)
                room_cells_[rooms[s]]++;
        }

        // A room that covers only part of a cell (on an 8 x 8 sample grid) is a street band.
        std::array<std::uint8_t, 64> dense{};
        std::array<std::uint8_t, 256> hits{};
        for (std::int32_t i = 0; i < 64; i++) {
            dense[static_cast<std::size_t>(i)] =
                room_at(cell.x * kCellSize + ((i & 7) * 2 + 1) * (kCellSize / 16),
                        cell.z * kCellSize + ((i >> 3) * 2 + 1) * (kCellSize / 16));
            hits[dense[static_cast<std::size_t>(i)]]++;
        }
        for (std::uint8_t room : dense) {
            if (hits[room] < 64)
                partial_[room] = 1;
        }
    });

    partial_[0] = 0;
    grid.for_each_cell([&](CellCoord cell) {
        for (const CellCoord& p : parked) {
            if (p == cell)
                return;
        }
        for (std::uint8_t room : sampled[cell_index(cell)]) {
            if (room_cells_[room] >= kMinRoomCells || partial_[room] != 0) {
                outdoor_[cell_index(cell)] = 1;
                count_++;
                return;
            }
        }
    });
}

bool OutdoorCells::outdoor(CellCoord cell) const {
    return CellGrid::contains(cell) && outdoor_[cell_index(cell)] != 0;
}

}  // namespace sh::world

namespace sh::world {

CellGrid& map_cells() {
    static CellGrid grid;
    return grid;
}

}  // namespace sh::world
