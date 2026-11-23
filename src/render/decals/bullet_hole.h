// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The bullet holes' picture, made by the engine instead of read from a file: a sheet of kBulletHoleGrid x
// kBulletHoleGrid different holes, each a punched core, a chipped crater of exposed material around it, a few
// cracks and a soot halo. A texel is not a colour but what the decal program of the primitive shader lights:
//
//   R, G   the surface normal's tilt along the cell's u and v (128 = flat), from the hole's height field
//   B      how much of the surface's light the spot keeps (128 = all of it: a mark only darkens)
//   A      how much of the wall the mark covers
//
// Every cell fades to nothing before its edge, so neighbours never bleed into each other under filtering.

#include <cstdint>
#include <vector>

namespace sh::render {

inline constexpr int kBulletHoleCell = 64;  // texels per side of one hole
inline constexpr int kBulletHoleGrid = 4;   // holes per side of the sheet
inline constexpr int kBulletHoleVariants = kBulletHoleGrid * kBulletHoleGrid;
inline constexpr int kBulletHoleSheet = kBulletHoleCell * kBulletHoleGrid;

// The sheet, RGBA8, kBulletHoleSheet texels per side. The same seed gives the same sheet.
std::vector<std::uint8_t> bullet_hole_sheet(std::uint32_t seed);

// How one hit looks: which hole of the sheet, how it is turned on the surface and how large it is. Derived from
// the hit point alone, so a hit never draws from the game's random numbers and looks the same in every run.
struct BulletHoleLook {
    int variant = 0;     // 0 .. kBulletHoleVariants - 1
    float angle = 0.f;   // radians
    float scale = 1.f;   // of the nominal size
    bool mirrored = false;
};
BulletHoleLook bullet_hole_look(std::int32_t x, std::int32_t y, std::int32_t z);

}  // namespace sh::render
