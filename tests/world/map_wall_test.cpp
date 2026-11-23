// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include "engine/world/map_wall.h"

SH_TEST(wall_shade_zero_angle) {
    const sh::world::WallShade s = sh::world::wall_shade(0);
    // sin(0) = 0, cos(0) = 4096: dim floors to zero, span = angle + 128 clamped.
    SH_CHECK(s.dim[0] == 0 && s.dim[1] == 0 && s.dim[2] == 0);
    SH_CHECK(s.span[0] == 320);  // (0x80 * Q12(2.5)) >> 12
}

SH_TEST(wall_point_player_x_wraps_like_uint16) {
    // A negative player x goes through the original's (u16) cast: +65536 offset.
    const sh::world::WallPoint a = sh::world::wall_point(0, 0, 0, 0, 0, 0, 0, -100, 0, 0);
    const sh::world::WallPoint b = sh::world::wall_point(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    SH_CHECK(a.x - b.x == 100 - 65536 || a.x - b.x == 100);
    // The engine reproduces the cast exactly: (u16)(-100) = 65436.
    SH_CHECK(a.x == 0 - static_cast<int>(static_cast<unsigned short>(-100)));
}

SH_TEST(wall_point_zero_terms_are_transparent) {
    const sh::world::WallPoint p = sh::world::wall_point(40960, 8192, 12288, 0, 0, 0, 0, 0, 0, 0);
    SH_CHECK(p.x == 40960 >> 4);  // only the Q12 -> Q8 shift remains
    SH_CHECK(p.y == 8192 >> 4);
    SH_CHECK(p.z == 12288 >> 4);
}

SH_TEST(wall_colors_lower_and_floor) {
    const unsigned char base[6] = {0x4C, 0x27, 0x7F, 10, 20, 30};
    const sh::world::WallColors c = sh::world::wall_colors(base, 0, 0);
    SH_CHECK(c.near_row[0] == 0x4C && c.near_row[1] == 0x27 && c.near_row[2] == 0x7F);
    SH_CHECK(c.far_row[0] == 10 && c.far_row[1] == 20 && c.far_row[2] == 30);
    // Lowering past zero floors at 0.
    const sh::world::WallColors d = sh::world::wall_colors(base, 16 * 100, 0);
    SH_CHECK(d.near_row[0] == 0 && d.far_row[2] == 0);
}
