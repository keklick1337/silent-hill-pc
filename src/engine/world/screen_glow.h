// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Light glow, lens flare and flashlight numbers (docs/RENDER.md section 5.3): the flashlight fade, the lens
// flare direction, the world brightness, the screen-edge ring points, the ring tint colours, the projection of a
// flare onto the screen and the vertices of the glow rings. Pure integer math on engine structs; the game turns the
// results into primitives.

#include <array>
#include <cstdint>

namespace sh::world {

// Flashlight fade: moves the intensity (Q12, 0..4096) towards on/off at four units per second.
std::int32_t flashlight_intensity_step(std::int32_t intensity, bool on, std::int32_t delta_time);

// Brightness of the world light (Q8, 0..256) from the zone's brightness intensity (Q12) and the user setting 0..7.
std::int32_t world_brightness(std::int32_t brightness_intensity, std::int32_t user_brightness);

// The grey the user's brightness level 0..7 adds to the frame: (level - bias) * 8 + 4, and nothing below the bias.
// The console's own mapping is bias 0 (level 3 adds 28): it was tuned for a TV that crushes the bottom of the signal;
// on a monitor the same grey reads as haze, so the display side shifts the levels down by `bias`.
std::int32_t brightness_level_grey(std::int32_t user_brightness, std::int32_t bias);

// world_brightness with the levels shifted down by `bias` (brightness_level_grey).
std::int32_t world_brightness(std::int32_t brightness_intensity, std::int32_t user_brightness, std::int32_t bias);

// Where the lens flare sits: the zone's flare mode (0 off, 1 follows the flashlight, 2..5 fixed sky directions) and
// whether the zone uses a lens flare at all.
struct LensFlare {
    std::int32_t alpha = 0;       // Q12
    bool follows_bone = false;    // the flare is attached to the lens flare bone, otherwise a fixed direction
    std::array<std::int16_t, 3> dir{};  // Q12 direction of the flare
};
LensFlare lens_flare_get(std::int32_t alpha, std::int32_t zone_flag, std::uint32_t mode,
                         const std::array<std::int16_t, 3>& light_rot);

struct ScreenPoint {
    std::int16_t x = 0;
    std::int16_t y = 0;
    bool operator==(const ScreenPoint& o) const { return x == o.x && y == o.y; }
};

// The 17 points of the ring around the screen edge (the first and last coincide), for a screen of w by h and a
// half width of the ring (half the width for a 4:3 frame, more when the view is wider).
std::array<ScreenPoint, 17> screen_ring_points(std::int32_t half_w, std::int32_t h);

// Colours of the ring tint, packed 0x00BBGGRR: [0..3] are the ring gradient steps (black, third, two thirds, full of
// the edge colour), `fan` is the colour of the centre fan.
struct RingColors {
    std::array<std::uint32_t, 4> steps{};
    std::uint32_t fan = 0;
};
RingColors ring_colors(std::uint8_t fan_r, std::uint8_t fan_g, std::uint8_t fan_b, std::uint8_t edge_r,
                       std::uint8_t edge_g, std::uint8_t edge_b);

struct Vec3 {
    std::int32_t x = 0, y = 0, z = 0;
};

// Projects a point of the view onto the screen (the flare lies on the plane at half the screen distance when the
// point is behind it). `screen_dist` is the projection distance, offset the screen centre.
Vec3 screen_point_project(const Vec3& from, const Vec3& dir, std::int32_t screen_dist, std::int32_t offset_x,
                          std::int32_t offset_y);

// Vertices of the four glow rings (17 points each) of a flare at the screen position `pos` (x, y in screen units,
// z the depth, Q12) of `size` (Q12) seen under two angles. x_scale stretches the ring about the flare for a wide
// view. Returns false when the flare is degenerate (depth or size zero): nothing to draw.
struct GlowRings {
    std::array<std::array<ScreenPoint, 17>, 4> ring{};
};
bool glow_rings_build(const Vec3& pos, std::int32_t size, std::int32_t angle0, std::int32_t angle1, float x_scale,
                      GlowRings& out);

}  // namespace sh::world
