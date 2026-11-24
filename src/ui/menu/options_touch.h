// SPDX-License-Identifier: GPL-3.0-or-later
// The natives of the Touch Controls page (gamedata/menus/options.lua) and of the performance overlay row.
#pragma once

namespace sh::menu {
class Registry;
}

namespace sh::ui {

void register_touch_rows(sh::menu::Registry& reg);

}  // namespace sh::ui
