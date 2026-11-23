// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/inventory/screen_input.h"

#include "engine/input/input_primitives.h"

namespace sh::inventory {

Directions read_directions(const PadFrame& pad, const DirectionBits& analog, const DirectionBits& direction)
{
    // The asymmetric zone (-65 is out, 64 is out) is the same one the rest of the game uses.
    const DirectionBits& set =
        sh::input::stick_outside_deadzone(pad.stick_x, pad.stick_y) ? analog : direction;

    Directions d;
    d.up_clicked = (pad.clicked & set.up) != 0;
    d.up_pulsed = (pad.pulsed & set.up) != 0;
    d.down_clicked = (pad.clicked & set.down) != 0;
    d.down_pulsed = (pad.pulsed & set.down) != 0;
    d.left_clicked = (pad.clicked & set.left) != 0;
    d.left_held = (pad.held & set.left) != 0;
    d.left_pulsed = (pad.pulsed & set.left) != 0;
    d.right_clicked = (pad.clicked & set.right) != 0;
    d.right_held = (pad.held & set.right) != 0;
    d.right_pulsed = (pad.pulsed & set.right) != 0;
    return d;
}

}  // namespace sh::inventory
