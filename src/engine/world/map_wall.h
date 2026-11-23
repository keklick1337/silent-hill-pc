// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The decision core of the map screen's wall trace: the two
// per-ring shade parameters, the source points of the 32x3 wall cone and the
// quad colours. Fixed-point integers on the game's macros (fixed_sin/fixed_cos
// are the game's values to the last bit); the projection (rtpt), the packets
// and the ordering table stay in the bridge.

#pragma once

#include <cstdint>

namespace sh::world {

// The shade parameters of the three wall bands (the original keeps them in its
// draw work area), from the cone angle (arg2, Q12).
struct WallShade {
    std::int32_t dim[3];   // negative darkening term, Q12
    std::int32_t span[3];  // radial reach, Q12
};
WallShade wall_shade(std::int32_t cone_angle_q12);

// One source point of the cone, relative to the player in Q8 as the original
// stored into its 16-bit vector — including the original's uint16 cast on the x
// player term and its absence on y and z.
struct WallPoint {
    std::int32_t x, y, z;
};
WallPoint wall_point(std::int32_t src_x, std::int32_t src_y, std::int32_t src_z,
                     std::int32_t rot_y_q12, std::int32_t sweep_q12, std::int32_t span,
                     std::int32_t dim, std::int32_t player_x_q8, std::int32_t player_y_q8,
                     std::int32_t player_z_q8);

// The two colour rows of a quad band: the base table's rows j and j+1, each
// entry lowered by (arg3 >> 4) + (arg2 >> 5) and floored at 0.
struct WallColors {
    std::uint8_t near_row[3];
    std::uint8_t far_row[3];
};
WallColors wall_colors(const std::uint8_t* base_two_rows, std::int32_t arg3, std::int32_t arg2);

}  // namespace sh::world
