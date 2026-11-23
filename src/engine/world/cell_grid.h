// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The cell grid of a map and the rules of the streamed cell window: which file
// fills each 40 m cell, how far a point is from a cell, which window slot a new cell replaces, which resident cell
// answers a collision query and which cells count as outdoors for the whole-town view. Positions are Q12 metres like
// the game state; cell distances are in the units the game keeps them in (Q8 metres, the IPD geometry scale).

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>

namespace sh::world {

struct CellCoord {
    std::int32_t x = 0;
    std::int32_t z = 0;
    friend bool operator==(const CellCoord&, const CellCoord&) = default;
};

inline constexpr std::int32_t kCellSize = 40 * 4096;         // Q12
inline constexpr std::int32_t kCellSizeGeometry = 40 * 256;  // Q8, the IPD geometry scale

// The cell holding a point (Q12), rounded toward negative infinity.
CellCoord cell_of(std::int32_t x, std::int32_t z);

// Distance (Q8) from a point given in Q8 to the nearest point of a cell; 0 inside it.
std::int32_t distance_to_cell(std::int32_t x_q8, std::int32_t z_q8, CellCoord cell);

// Distance (Q8) from a point given in Q12 to a cell; exterior windows count one extra metre band as inside.
std::int32_t padded_distance_to_cell(std::int32_t x, std::int32_t z, CellCoord cell, bool exterior);

// The signed byte two cell-name digits stand for ("0A" is 10, "FF" is -1); nothing for characters that are not
// digits. The digits follow the original name parser exactly, including what it accepts outside 0-9 and A-F.
std::optional<std::int32_t> parse_cell_digits(char high, char low);

// The cell files of a map: x from -8 to 7, z from -8 to 10. A file is named by the map's prefix and the cell's two
// coordinates in two-digit hex ("THR" + "0205" -> cell (2, 5)).
class CellGrid {
public:
    static constexpr std::int32_t kMinX = -8, kEndX = 8;
    static constexpr std::int32_t kMinZ = -8, kEndZ = 11;
    static constexpr std::int32_t kNone = -1;

    // Empties every cell.
    void clear();
    // Places `file` at the cell its name gives when the name starts with `prefix`; other names are ignored.
    void add_file(std::string_view prefix, std::string_view name, std::int32_t file);

    static bool contains(CellCoord cell);
    // The file of a cell; kNone for an empty cell or one outside the grid.
    std::int32_t file_at(CellCoord cell) const;
    // Replaces the file of a cell (a cell swapped by an event); ignored outside the grid.
    void set(CellCoord cell, std::int32_t file);

    template <typename Fn>
    void for_each_cell(Fn&& fn) const {
        for (std::int32_t z = kMinZ; z < kEndZ; z++)
            for (std::int32_t x = kMinX; x < kEndX; x++)
                if (file_at({x, z}) != kNone)
                    fn(CellCoord{x, z});
    }

private:
    static std::size_t index(CellCoord cell) {
        return static_cast<std::size_t>((cell.z - kMinZ) * (kEndX - kMinX) + (cell.x - kMinX));
    }
    std::array<std::int16_t, (kEndX - kMinX) * (kEndZ - kMinZ)> files_ = empty_files();
    static constexpr std::array<std::int16_t, (kEndX - kMinX) * (kEndZ - kMinZ)> empty_files() {
        std::array<std::int16_t, (kEndX - kMinX) * (kEndZ - kMinZ)> a{};
        a.fill(static_cast<std::int16_t>(kNone));
        return a;
    }
};

// ---- The streamed window -------------------------------------------------------------------------------------

// One slot of the resident cell window as the replacement rule sees it.
struct WindowSlot {
    bool occupied = false;              // a cell is assigned (loading or loaded)
    std::uint32_t outside_count = 0;    // ticks the slot spent outside both sample points' reach
    std::int32_t material_count = 0;    // half-page materials of the loaded cell (0 while loading)
    std::int32_t padded_distance = 0;   // padded distance of the first sample point to the cell
};

// The slot a new cell goes into, or nothing when every slot must stay. An empty slot always wins. Interiors then take
// the slot that has been out of reach the longest; exteriors the one with the most half-page materials, then the
// farthest from the first sample point, never one the point is inside of.
std::optional<std::size_t> pick_window_slot(std::span<const WindowSlot> slots, bool exterior);

// The outside counter after a sample: grows while both sample points are away from the cell, else restarts.
inline std::uint32_t next_outside_count(std::uint32_t count, std::int32_t distance0, std::int32_t distance1) {
    return distance0 > 0 && distance1 > 0 ? count + 1 : 0;
}

// ---- Collision cells -------------------------------------------------------------------------------------------

struct ResidentCell {
    CellCoord cell;
    bool ready = false;  // loaded and prepared: its collision can be queried
};

struct CollisionCellLookup {
    enum class Kind : std::uint8_t {
        Resident,  // `slot` answers
        Missing,   // the grid has a file there but no ready slot holds it: nothing answers
        Open,      // the grid has no file there: the map's open-ground fallback answers
    };
    Kind kind = Kind::Open;
    std::size_t slot = 0;
};

// Which resident cell answers a collision query at a point (Q12).
CollisionCellLookup collision_cell_at(std::span<const ResidentCell> slots, const CellGrid& grid, std::int32_t x,
                                      std::int32_t z);

// Whether a resident cell takes part in the collision of a tick: with the whole map resident (exterior preload),
// only the 3 x 3 cells around the window centre do, as the console never had farther cells loaded.
inline bool in_collision_scope(CellCoord cell, CellCoord centre, bool whole_map_resident) {
    return !whole_map_resident || (cell.x >= centre.x - 1 && cell.x <= centre.x + 1 && cell.z >= centre.z - 1 &&
                                   cell.z <= centre.z + 1);
}

// ---- Outdoor cells (whole-town view) ---------------------------------------------------------------------------

// The room of a map position (Q12), as the map defines its rooms; 0 is the void around the authored areas.
using RoomAt = std::function<std::uint8_t(std::int32_t x, std::int32_t z)>;

// Which cells of the grid are outdoors: a cell is when one of five samples (centre and four corners 14 m in) lies in
// a room that spans at least three cells, or in a room that only partly covers some cell (a street band). Swapped
// cells (`parked`) never count.
class OutdoorCells {
public:
    void classify(const CellGrid& grid, std::span<const CellCoord> parked, const RoomAt& room_at);
    bool outdoor(CellCoord cell) const;
    std::int32_t count() const { return count_; }
    // Distinct cells a room was sampled in, and whether it only partly covers a cell (for the log).
    std::uint16_t room_cells(std::uint8_t room) const { return room_cells_[room]; }
    bool room_partial(std::uint8_t room) const { return partial_[room] != 0; }

private:
    std::array<std::uint8_t, (CellGrid::kEndX - CellGrid::kMinX) * (CellGrid::kEndZ - CellGrid::kMinZ)> outdoor_{};
    std::array<std::uint16_t, 256> room_cells_{};
    std::array<std::uint8_t, 256> partial_{};
    std::int32_t count_ = 0;
};

}  // namespace sh::world

namespace sh::world {

// The cell grid of the loaded map (filled when a map's cells are set up).
CellGrid& map_cells();

}  // namespace sh::world
