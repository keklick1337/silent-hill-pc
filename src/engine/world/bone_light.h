// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The light of one character bone (docs/RENDER.md section 6): what the character drawer needs to colour
// the vertices that hang on a bone, from the map's light state. Light mode 0 gives a flat colour (the world tint
// darkened by the map light); modes 1 and 2 give the inputs of the single-normal colour operation: a light matrix
// whose first row is the light vector in the bone's frame, the light colour matrix and the background colour.

#include <cstdint>

#include "engine/world/world_atmosphere.h"

namespace sh::world {

struct BoneLight {
    std::uint8_t ambient[3]{};  // light mode 0
    std::int16_t light[9]{};    // light matrix, row-major, Q12
    std::int16_t color[9]{};    // light colour matrix, row-major, Q12
    std::int32_t back[3]{};     // background colour, 0..255 << 4
};

// The light of the bone whose world frame is `bone` (null: the point light is not moved into a frame and the last
// model frame's light stays). Turning the point light into the bone's frame updates the atmosphere's local light
// (set_model_light), as every lit-model draw does.
BoneLight bone_light(WorldAtmosphere& atmosphere, const ModelFrame* bone);

}  // namespace sh::world
