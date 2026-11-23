// SPDX-License-Identifier: GPL-3.0-or-later
// Asks the system what it can draw with, before any game window exists: an OpenGL 3.3 context on a hidden window
// (and whether it draws on the CPU) and a Vulkan device as the game's Vulkan renderer would make it, without a
// surface, read for its kind, memory and ray tracing, then destroyed.
#pragma once

#include "engine/app/quick_setup/setup_model.h"

namespace sh::quick_setup {

// SDL's video subsystem must be up.
Hardware probe_hardware();

// One line for the log.
std::string describe(const Hardware& hw);

}  // namespace sh::quick_setup
