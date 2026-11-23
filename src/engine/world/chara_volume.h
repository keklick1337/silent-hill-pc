#pragma once

// A character's collision volume for rays, as the game sets it up: a vertical cylinder in Q.8
// world units around a centre, between a ground-side and a ceiling-side height (Y grows down,
// so `low` is the smaller value). The builder is the game's integer pipeline exactly: the
// Q.12 inputs are truncated to Q.8 by a shift, the intermediate stores wrap to 16 bits where
// the game's state struct does, and every product goes through a 64-bit intermediate.

#include <cstdint>

namespace sh::world {

// Which of the character's two collision shapes the volume is built from.
enum class VolumePath : std::uint8_t {
    Cylinder,  // the walk/move cylinder: the wide radius, the box's bottom as the top bound
    Box,       // the bullet path: the narrow radius, the box's height as the top bound
};

// The character's collision fields, Q.12. Heights are relative to `y` and offsets to (x, z).
struct CharaVolumeSource {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    std::int32_t cylinder_radius = 0;  // move cylinder radius
    std::int32_t box_radius = 0;       // the narrow bullet cylinder radius
    std::int32_t box_top = 0;          // ground-side bound of the box
    std::int32_t box_bottom = 0;       // cylinder path: ceiling-side bound
    std::int32_t box_height = 0;       // box path: ceiling-side bound
    std::int32_t cylinder_offset_x = 0;
    std::int32_t cylinder_offset_z = 0;
    std::int32_t box_offset_x = 0;
    std::int32_t box_offset_z = 0;
    VolumePath path = VolumePath::Cylinder;
};

// Widens the box path so the whole visible body is hittable by a bullet (a pure gameplay
// overlay, off in the base game).
struct VolumeInflate {
    bool active = false;
    std::int32_t vert_mul = 4096;  // Q.12, band growth each side, in radius units
    std::int32_t rad_mul = 4096;   // Q.12, radius growth, in radius units
};

// The built volume, Q.8. Field order and meaning match RayTarget, so the game side can copy
// it over without reordering.
struct CharaVolume {
    std::int32_t x = 0;  // world positions do not fit 16 bits in Q.8; only the heights wrap
    std::int32_t z = 0;
    std::int16_t low = 0;    // ground-side height (Q.8, Y grows down)
    std::int16_t high = 0;   // ceiling-side height
    std::int16_t radius = 0;
};

// Builds the volume. The radius truncates to 16 bits after each of its assignments, as the
// game's state struct stores it in 16 bits between the steps.
CharaVolume build_chara_volume(const CharaVolumeSource& source, const VolumeInflate& inflate);

}  // namespace sh::world
