#pragma once

// Collision of one map cell as the world module keeps it. Loaded from the
// SH_collision extension of the unified map file (maps/areas/<AREA>.glb); the engine does not read the packed cell
// files for collision.
//
// Units: positions and heights are 1/256 of a world unit (Q.8) relative to the cell origin, slopes and edge
// directions are 1/4096 (Q.12). Y grows downwards: a smaller height is higher up.
//
// The cell is a grid of `columns` x `rows` squares of `square_size`. Each square lists, in `edge_pool`, the indices of
// the wall edges and pillars that touch it (an index below edges.size() is an edge, the rest are pillars after the
// edges) and, in `floor_pool`, the floors that cover it. Square k uses the entries [squares[k].first, squares[k+1].first)
// of each pool, so `squares` has one closing entry more than the grid.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sh::world {

struct Point3 {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int16_t z = 0;
};

// A floor plane. Its height at (x, z) is base_height + slope_x * (x - anchor_x) + slope_z * (z - anchor_z).
struct FloorPlane {
    std::int16_t anchor_x = 0;
    std::int16_t base_height = 0;
    std::int16_t anchor_z = 0;
    std::uint8_t ground_type = 0;     // footstep material; 12 = no ground
    std::uint8_t height_disabled = 0; // 1 = the plane gives no ground (a hole); other values are stored as found
    std::uint8_t side_slot = 0;       // which of the four edge-crossing slots this plane competes in
    std::uint8_t enable_group = 0;    // bit of the collision group mask that switches the plane on
    std::int16_t slope_x = 0;
    std::int16_t slope_z = 0;
    std::uint16_t packed_flags = 0;   // the four fields above as stored, for checks
};

// A wall edge: a segment from `start_vertex` along the unit direction (dir_x, dir_z) for `length`, with a floor on
// each side (255 = none). A side without a floor is a wall face.
struct WallEdge {
    std::int16_t dir_x = 0;           // 14-bit signed
    std::int16_t dir_z = 0;           // 14-bit signed
    std::int16_t length = 0;
    std::uint8_t group_high = 0;      // enable group = group_high * 4 + group_low
    std::uint8_t group_low = 0;
    std::uint8_t end_vertex = 0;
    std::uint8_t start_vertex = 0;
    std::uint8_t floor_left = 0;      // the side with a non-negative perpendicular coordinate
    std::uint8_t floor_right = 0;
    std::uint16_t packed_x = 0;       // dir_x and group_high as stored
    std::uint16_t packed_z = 0;       // dir_z and group_low as stored
};

// A round obstacle (pole, hydrant): a vertical cylinder of `radius` whose top is at `position.y`.
struct Pillar {
    std::uint8_t ground_type = 0;
    std::uint8_t height_disabled = 0;
    std::uint8_t enable_group = 0;
    Point3 position;
    std::int16_t radius = 0;
    std::uint16_t packed_flags = 0;   // ground type, height flag, group and the two unused fields as stored
};

struct SquareLists {
    std::int16_t first = 0;           // into edge_pool
    std::int16_t first_floor = 0;     // into floor_pool
};

struct CellCollision {
    std::string name;                 // cell file stem, upper case ("ER0402")
    std::int32_t origin_x = 0;        // as stored in the cell file; the game moves it when it places the cell
    std::int32_t origin_z = 0;
    std::int16_t square_size = 0;
    std::uint8_t columns = 0;
    std::uint8_t rows = 0;
    std::uint16_t floor_pool_size_field = 0;  // stored count of floor_pool, kept for checks

    std::vector<Point3> vertices;
    std::vector<FloorPlane> floors;
    std::vector<WallEdge> edges;
    std::vector<Pillar> pillars;
    std::vector<SquareLists> squares;
    std::vector<std::uint8_t> edge_pool;
    std::vector<std::uint8_t> floor_pool;

    // The per-pass visit ledger as stored in the cell file (all zero on the USA 1.1 disc).
    std::uint8_t stored_pass = 0;
    std::array<std::uint8_t, 256> stored_marks{};

    // Not collision, but kept with the cell: the lamp glows of the cell in file order (the cell root's extras
    // "glows"). Cell space (Q8, the placed cell's corner is 0); `kind` selects the glow effect (0 and 1 are drawn).
    struct Glow {
        Point3 position;
        std::int16_t kind = 0;
    };
    std::vector<Glow> glows;

    bool empty() const { return floors.empty() && edges.empty() && pillars.empty(); }
};

}  // namespace sh::world
