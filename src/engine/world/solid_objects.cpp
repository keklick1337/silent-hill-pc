// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/world/solid_objects.h"

namespace sh::world {

std::vector<SolidCylinder>& solid_cylinders() {
    static std::vector<SolidCylinder> s_cylinders;
    return s_cylinders;
}

}  // namespace sh::world
