// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/inventory/pickup_anim.h"

#include <algorithm>

namespace sh::inventory {

namespace {
constexpr std::int32_t kHalf = 2048;
constexpr std::int32_t kFull = 4096;
}  // namespace

PickupAnim pickup_begin() {
    PickupAnim anim;
    anim.scale = kFull;  // the host sets the model's scale to full right after the start
    return anim;
}

bool pickup_step(PickupAnim& anim, std::int32_t frame_time, std::int16_t tilt_x, std::int16_t tilt_z) {
    switch (anim.state) {
    case 0:
        if (anim.grow >= kHalf) {
            anim.state = 1;
        }
        // The size is doubled: (grow << 12) >> 11, in 32 bits like the original.
        anim.scale = static_cast<std::int32_t>(static_cast<std::uint32_t>(anim.grow) << 12) >> 11;
        anim.rot_x = tilt_x;
        anim.rot_y = 0;
        anim.rot_z = tilt_z;
        break;
    case 1:
        anim.scale = kFull;
        anim.state = 2;
        break;
    default:
        break;
    }
    anim.grow = std::clamp(anim.grow + frame_time * 2, 0, kHalf);
    return anim.state > 0;
}

void pickup_spin(PickupAnim& anim, std::int32_t frame_time) {
    anim.rot_y = static_cast<std::int16_t>(static_cast<std::uint16_t>(anim.rot_y) +
                                           static_cast<std::uint16_t>(frame_time >> 1));
}

}  // namespace sh::inventory
