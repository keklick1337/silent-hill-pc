// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// One movement step of a character against walls, other characters and the ground: the
// displacement from its speed, heading and fall speed (step_offset), the world's answer, then the character is moved
// and lands on the ground it reached. The world's collision and the character's state are reached through MoveView,
// which the game implements over its records (src/engine/chara/game/combat_host.cpp).

#pragma once

#include "engine/combat/combat_math.h"

#include <cstdint>

namespace sh::combat {

// The world's answer to a step: how far the character may really move and the ground under the end point.
struct MoveSurface {
    Vec3 offset;
    std::int32_t ground_height = 0;
    std::int32_t ground_type = 0;
};

struct MoverState {
    Vec3 position;
    std::int32_t move_speed = 0;   // Q12 metres per second
    std::int16_t heading = 0;      // Q12 angle
    std::int32_t fall_speed = 0;   // Q12 metres per second, +Y down
};

class MoveView {
public:
    virtual ~MoveView() = default;
    virtual std::int32_t delta_time() const = 0;
    virtual MoverState mover(int slot) const = 0;
    virtual void set_mover(int slot, const Vec3& position, std::int32_t fall_speed) = 0;
    // The wall/character/ground test of the step `offset`; returns the game's wall response code.
    virtual std::int32_t collide(int slot, const Vec3& offset, MoveSurface& surface) = 0;

    // The XZ offsets of the collision box and cylinder from the position, and the heading they turn with.
    struct ShapeOffsets {
        PlanarOffset box;
        PlanarOffset cylinder;
    };
    virtual ShapeOffsets shape_offsets(int slot) const = 0;
    virtual void set_shape_offsets(int slot, const ShapeOffsets& offsets) = 0;
    virtual std::int16_t yaw(int slot) const = 0;
};

// Turns the character's shape offsets by its heading (in place: the offsets are read before any is written).
void turn_shape_offsets(MoveView& view, int slot);

// Moves the character in `slot` one step plus the push (x, z). Returns the wall response code; `surface`
// (optional) receives the world's answer.
std::int32_t move_character(MoveView& view, int slot, std::int16_t push_x, std::int16_t push_z,
                            MoveSurface* surface = nullptr);

}  // namespace sh::combat
