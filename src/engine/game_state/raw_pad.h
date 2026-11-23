// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// A pad as the device reports it to the game (Session::game.raw_pad, the pads' raw state); the PC input
// (input/sdl_pad.h) writes it every frame.

#pragma once

#include <cstdint>

namespace sh::game {

struct RawPad {
    std::uint8_t status;
    std::uint8_t kind;  // low nibble: received bytes / 2, high nibble: terminal type (0x41 digital, 0x73 analog)
    std::uint16_t buttons;  // active low
    std::uint8_t right_x, right_y, left_x, left_y;  // 128 is centre
};

}  // namespace sh::game
