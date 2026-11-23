// SPDX-License-Identifier: GPL-3.0-or-later
//
// The running game's steps on the world atmosphere (engine/world/world_atmosphere.h) that also touch other engine
// state: the map's first atmosphere, the frame light with the console's colour overrides, and the point light placed
// from the game's bone frames (the first-person view moves it to the eye).
#pragma once

#include <cstdint>

#include "engine/world/world_atmosphere.h"

namespace sh::world {

// The atmosphere of a map before its first frame: the screen glow's shapes, the water effect, no far colour, the
// atmosphere's start values.
void world_env_init();

// The frame light: the map's preset (mode, ambient, cap), the world tint and the screen brightness, scaled by the
// console's light colour overrides (`fl`, `wl`).
void frame_light_set(std::uint8_t mode, std::int32_t ambient, std::uint8_t cap, std::int32_t tint_r,
                     std::int32_t tint_g, std::int32_t tint_b, std::int32_t brightness);

// The point light: its direction `rotation` turned by the frame `direction_bone` (none: as given), its position
// (x, y, z) (Q12) placed in the frame `position_bone` (none: world). In the first-person view the lit flashlight
// comes from the eye along the view; the shadow map keeps the real position.
void point_light_set(std::int32_t intensity, std::int16_t lens_flare, void* direction_bone, void* position_bone,
                     const Vec3s16& rotation, std::int32_t x, std::int32_t y, std::int32_t z, void* water_zones);

}  // namespace sh::world
