// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The per-frame decisions of the 2D screen effects (docs/RENDER.md section 5.3): the flashlight cone the
// per-pixel renderer gets in view space, the debounce of "this room is lit by the flashlight" and the grey of the
// brightness overlay. The game draws the packets.

#include <array>
#include <cstdint>

#include "engine/math/fixed_geometry.h"

namespace sh::world {

// World-to-view matrix of the camera: Q12 rotation, Q8 translation.
struct ViewMatrix {
    math::Matrix3 rot;
    math::Vec3i trans;
};

struct FlashlightConeIn {
    ViewMatrix view;
    math::Vec3i light_position;          // Q12 world position of the light
    math::Vec3i shadow_position;         // Q12 position the shadow map is cast from
    math::Vec3s direction;               // Q12 world direction of the beam
    math::Matrix3 color_matrix;          // the room's light colour matrix
    bool tinted_by_room = false;        // colour from the matrix (classic) instead of the fixed warm white
};

struct FlashlightCone {
    std::array<float, 3> position{};
    std::array<float, 3> shadow_position{};
    std::array<float, 3> direction{};
    std::array<float, 3> color{};
};

FlashlightCone flashlight_cone(const FlashlightConeIn& in);

// Holds the last stable answer of a boolean that may flicker for single frames; a new answer is adopted once it has
// been seen `frames` times in a row. The first answer is adopted at once.
class StableFlag {
public:
    explicit StableFlag(int frames) : frames_(frames) {}
    bool update(bool raw);

private:
    int frames_;
    int state_ = -1;
    int candidate_ = -1;
    int agree_ = 0;
};

// Grey of the brightness overlay (all four corners), as a packed colour word with a zero code byte.
std::uint32_t overlay_gray(std::int32_t brightness);

}  // namespace sh::world
