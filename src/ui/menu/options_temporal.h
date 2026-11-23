// SPDX-License-Identifier: GPL-3.0-or-later
// The natives of the Graphics rows of TAA, the upscalers and frame pacing (options_temporal.cpp).
#pragma once

namespace sh::menu {
class Registry;
}

namespace sh::ui {

void register_temporal_rows(sh::menu::Registry& reg);

}  // namespace sh::ui
