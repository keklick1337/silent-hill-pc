// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#pragma once

// The solid parts of the world objects drawn now ("collision" of world_objects.json): upright cylinders a moving
// character is stopped by, as by another character that never gives way. The world objects' runtime sets the list
// every frame; the movement pass reads it. Empty for the game's own maps.

#include <cstdint>
#include <vector>

namespace sh::world {

struct SolidCylinder {
    std::int32_t x = 0;  // Q19.12 world
    std::int32_t z = 0;
    std::int32_t top = 0;     // the smaller Y (Y grows downwards)
    std::int32_t bottom = 0;
    std::int32_t radius = 0;
    std::uint8_t pass_stamp = 0;  // the movement pass's own mark
};

std::vector<SolidCylinder>& solid_cylinders();

}  // namespace sh::world
