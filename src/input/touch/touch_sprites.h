// SPDX-License-Identifier: GPL-3.0-or-later
//
// The touch controls' pictures as coverage masks at the exact size they show on the screen: discs, rings, soft
// shadows and the icons (touch_icons.h), anti-aliased. The controls draw each mask as one quad tinted with one
// colour, texel for pixel, so edges stay smooth at any screen density and render scale. No SDL, no renderer.
#pragma once

#include <cstdint>
#include <vector>

#include "input/touch/touch_icons.h"

namespace sh::input::touch {

// A square of coverage bytes (0..255), top row first, whose centre is the point the shape is drawn around: the
// side is even, so the centre lies on a pixel corner and a quad at a whole pixel keeps the edges where they were
// rasterised.
struct Mask {
    int size = 0;
    std::vector<std::uint8_t> alpha;
};

// A filled disc of `radius` pixels, its edge one pixel wide.
Mask disc_mask(float radius);
// A circle line of `radius` (to the middle of the line) and `width` pixels.
Mask ring_mask(float radius, float width);
// A soft halo around a disc of `radius`: full inside, fading out over `spread` pixels outside (a shadow or a glow).
Mask halo_mask(float radius, float spread);
// The icon with its viewBox square `side` pixels across, sampled 4x4 times per pixel.
Mask icon_mask(const VectorIcon& icon, float side);

}  // namespace sh::input::touch
