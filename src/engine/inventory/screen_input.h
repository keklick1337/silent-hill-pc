// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The four directions the item screens read from pad 1: which of the pad word's two direction sets counts this frame. A stick
// pushed past the dead zone on either axis is read through its own analog set, otherwise the
// shared direction set (digital pad, stick within the zone) is. The pad word's bit layout comes
// from the caller. Standard library only.

#pragma once

#include <cstdint>

namespace sh::inventory {

// The bits of one direction set in the pad words.
struct DirectionBits {
    std::uint32_t up = 0, right = 0, down = 0, left = 0;
};

// Pad 1 this frame.
struct PadFrame {
    std::uint32_t clicked = 0;  // went down this frame
    std::uint32_t held = 0;
    std::uint32_t pulsed = 0;   // down, repeating while held
    int stick_x = 0, stick_y = 0;  // left stick, -128..127
};

// What the screens test. Up and down have no held state: nothing on the screens reads one.
struct Directions {
    bool up_clicked = false, up_pulsed = false;
    bool down_clicked = false, down_pulsed = false;
    bool left_clicked = false, left_held = false, left_pulsed = false;
    bool right_clicked = false, right_held = false, right_pulsed = false;

    bool operator==(const Directions&) const = default;
};

// `analog`: the stick's own set; `direction`: the shared one.
Directions read_directions(const PadFrame& pad, const DirectionBits& analog, const DirectionBits& direction);

}  // namespace sh::inventory
