// SPDX-License-Identifier: GPL-3.0-or-later
// The natives of the ray tracing rows of Options > Graphics: setting.ray_tracing and the effect rows after it.
#pragma once

namespace sh::menu {
class Registry;
}

void Pc_OptionsRayTracing_Register(sh::menu::Registry& reg);

// True on the native Vulkan renderer with a GPU that has the KHR ray tracing extensions.
bool Pc_OptionsRayTracing_Available();
