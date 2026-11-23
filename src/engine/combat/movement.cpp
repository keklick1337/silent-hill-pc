// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/combat/movement.h"

namespace sh::combat {

std::int32_t move_character(MoveView& view, int slot, std::int16_t push_x, std::int16_t push_z,
                            MoveSurface* surface) {
    const MoverState state = view.mover(slot);
    const Vec3 offset = step_offset(view.delta_time(), state.move_speed, state.heading, state.fall_speed, push_x,
                                    push_z);
    MoveSurface answer;
    const std::int32_t code = view.collide(slot, offset, answer);

    // Read again: the collision test may have changed the character.
    const MoverState after = view.mover(slot);
    Vec3 position{after.position.x + answer.offset.x, after.position.y + answer.offset.y,
                  after.position.z + answer.offset.z};
    std::int32_t fall_speed = after.fall_speed;
    if (position.y > answer.ground_height) {
        position.y = answer.ground_height;
        fall_speed = 0;
    }
    view.set_mover(slot, position, fall_speed);
    if (surface != nullptr) {
        *surface = answer;
    }
    return code;
}

void turn_shape_offsets(MoveView& view, int slot) {
    const MoveView::ShapeOffsets from = view.shape_offsets(slot);
    const std::int16_t yaw = view.yaw(slot);
    MoveView::ShapeOffsets to;
    to.box = rotate_offset(from.box.x, from.box.z, yaw);
    to.cylinder = rotate_offset(from.cylinder.x, from.cylinder.z, yaw);
    view.set_shape_offsets(slot, to);
}

}  // namespace sh::combat
