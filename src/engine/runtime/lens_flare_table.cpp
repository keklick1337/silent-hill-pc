// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The lens flare sprite table (engine/runtime/lens_flare_table.h): the sprites' heights, distances, angles and sizes,
// and their placement around a view frame.

#include "engine/runtime/lens_flare_table.h"

#include "engine/world/fixed_trig.h"

namespace {

using sh::runtime::LensSprite;

// height, distance, angle (Q12 turns), half width, height; the rest is filled by the placement. Rows 17.. are unused.
LensSprite s_sprites[sh::runtime::kLensSpriteCount] = {
    {89, 102, 1820, 542, 387, 0, {}, {}},    {21, 306, 3413, 573, 356, 0, {}, {}},
    {4, 306, 682, 558, 356, 0, {}, {}},      {-30, 289, 2275, 527, 418, 0, {}, {}},
    {-64, 255, 3072, 527, 465, 0, {}, {}},   {-115, 306, 341, 449, 387, 0, {}, {}},
    {-200, 289, 1137, 589, 465, 0, {}, {}},  {-285, 221, 2048, 496, 387, 0, {}, {}},
    {-370, 272, 2844, 465, 387, 0, {}, {}},  {-455, 255, 3413, 449, 387, 0, {}, {}},
    {-642, 170, 0, 418, 310, 0, {}, {}},     {-727, 136, 910, 387, 310, 0, {}, {}},
    {13, 30, 2503, 270, 270, 0, {}, {}},     {-64, 60, 568, 360, 240, 0, {}, {}},
    {-115, 100, 2048, 340, 190, 0, {}, {}},  {-141, 30, 1137, 270, 240, 0, {}, {}},
    {-192, 70, 3640, 340, 180, 0, {}, {}},
};

void* s_view_frame = nullptr;

}  // namespace

sh::runtime::LensSprite* sh::runtime::lens_sprites() { return s_sprites; }
void* sh::runtime::lens_flare_view_frame() { return s_view_frame; }

extern "C" {

void LensFlare_SpriteTableInit(void* view_frame) {
    s_view_frame = view_frame;
    for (LensSprite* s = s_sprites; s < s_sprites + sh::runtime::kLensSpriteCount; s++) {
        s->facing.z = static_cast<std::int16_t>(-sh::world::fixed_cos(s->angle));
        s->facing.x = static_cast<std::int16_t>(-sh::world::fixed_sin(s->angle));
        s->facing.y = 0;
        s->position.z = static_cast<std::int16_t>((s->distance * sh::world::fixed_cos(s->angle)) >> 12);
        s->position.x = static_cast<std::int16_t>((s->distance * sh::world::fixed_sin(s->angle)) >> 12);
        s->position.y = s->height;
    }
}

}  // extern "C"
