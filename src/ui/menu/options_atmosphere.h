// SPDX-License-Identifier: GPL-3.0-or-later
// The natives of the Graphics rows of ambient occlusion and volumetric fog (options_atmosphere.cpp).
#pragma once

namespace sh::menu {
class Registry;
}

namespace sh::ui {

void register_atmosphere_rows(sh::menu::Registry& reg);

}  // namespace sh::ui
