// SPDX-License-Identifier: GPL-3.0-or-later
// Draw records of the effects module. The engine decides what
// an effect draws; the game's bridge turns each record into its primitive. Positions are
// screen pixels, texture coordinates texels, the depth slot the ordering-table entry.

#pragma once

#include <array>
#include <cstdint>

namespace sh::effects {

struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

// Where a texture lives: the game's texture page word and the palette's place in video
// memory (x in pixels, a multiple of 16; y the row).
struct TextureRef {
    std::uint16_t page = 0;
    std::uint16_t palette_x = 0;
    std::uint16_t palette_y = 0;
};

struct EffectPrim {
    enum class Kind : std::uint8_t {
        Quad,    // four corners in the order 0 1 2 3 (two triangles 0-1-2 and 1-2-3)
        Sprite,  // an axis-aligned box of w x h texels, one texel per pixel
    };
    Kind kind = Kind::Quad;
    bool gouraud = false;           // Quad: one colour per corner, else rgb[0] for all
    bool semi_transparent = false;
    // false: the record leaves the colour as it is (an effect the original drew with its
    // colour never set).
    bool has_color = true;
    std::array<Rgb, 4> rgb{};
    std::array<std::int16_t, 4> x{};  // Sprite: x[0], y[0] is the top left corner
    std::array<std::int16_t, 4> y{};
    std::array<std::uint8_t, 4> u{};  // Sprite: u[0], v[0]
    std::array<std::uint8_t, 4> v{};
    std::int16_t w = 0;  // Sprite size
    std::int16_t h = 0;
    TextureRef texture;
    std::int32_t slot = 0;  // ordering-table entry
    // The record's view depth (screen z, projection units) when it stands in the 3D scene: the renderer tests it
    // against the world's depth there, where the original only had the ordering table. 0: not given.
    std::int32_t view_depth = 0;
};

// A point the host projected: screen position and the depth word (screen z / 4).
struct ScreenPoint {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int32_t depth = 0;
};

// World position, game units (Q12, 4096 = 1 m).
struct Vec3 {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
};

// A position relative to the view origin in geometry units (Q8, 256 = 1 m), as the
// projection takes it.
struct Vec3s {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int16_t z = 0;
};

// World position minus the view origin, Q12 -> Q8; each axis truncated to 16 bits.
inline Vec3s relative_q8(const Vec3& p, const Vec3& origin) {
    return Vec3s{static_cast<std::int16_t>((p.x - origin.x) >> 4), static_cast<std::int16_t>((p.y - origin.y) >> 4),
                 static_cast<std::int16_t>((p.z - origin.z) >> 4)};
}

}  // namespace sh::effects
