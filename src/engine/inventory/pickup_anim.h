// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The world item-pickup animation: the picked-up model
// grows from nothing to full size over the first quarter second while it holds the display tilt
// of its item, then spins at half a turn per second. The module owns the numbers; the host
// draws the model and lights. Standard library only.

#pragma once

#include <cstdint>

namespace sh::inventory {

// Fixed point 20.12 and angle 4096 = full turn, as everywhere in the game state.
struct PickupAnim {
    std::int32_t state = 0;  // 0 growing, 1 snap to full size, 2 spinning
    std::int32_t grow = 0;   // 0 .. 2048 (half a unit), +2 * frame time per step
    std::int32_t scale = 0;  // model scale on all three axes (4096 = 1.0)
    std::int16_t rot_x = 0;
    std::int16_t rot_y = 0;  // wraps
    std::int16_t rot_z = 0;
};

// Pickup start: everything zero, full-size scale reserved for the later states.
PickupAnim pickup_begin();

// One frame before the draw: pose from the state, then the growth timer. `tilt_x`/`tilt_z` are
// the item's display tilt. Returns true once the animation is past its first state.
bool pickup_step(PickupAnim& anim, std::int32_t frame_time, std::int16_t tilt_x, std::int16_t tilt_z);

// After the draw: the spin, 180 degrees per second (frame time is 4096 per second).
void pickup_spin(PickupAnim& anim, std::int32_t frame_time);

}  // namespace sh::inventory
