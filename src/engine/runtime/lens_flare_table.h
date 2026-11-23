// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The lens flare sprite table: the sprites the billboard drawer draws in groups around a point (its rows are runs of
// this table). Each sprite sits at its distance along its angle around the frame, at its
// height, facing back along that angle; LensFlare_SpriteTableInit (C linkage) places them for a view frame.

#include <cstdint>

#include "formats/game/model_records.h"

namespace sh::runtime {

struct LensSprite {
    std::int16_t height;         // Q12
    std::int16_t distance;       // Q12
    std::int16_t angle;          // Q12 turns
    std::int16_t half_width;     // Q12 at the reference distance (the billboard quad's half width)
    std::int16_t sprite_height;  // Q12 (the billboard quad's height)
    std::int16_t reserved;
    sh::formats::records::ShortVec4 facing;    // Q12 unit vector
    sh::formats::records::ShortVec4 position;  // Q12
};

inline constexpr int kLensSpriteCount = 26;

// The table (placed by the last LensFlare_SpriteTableInit) and the view frame it was placed for (null before).
LensSprite* lens_sprites();
void* lens_flare_view_frame();

}  // namespace sh::runtime
