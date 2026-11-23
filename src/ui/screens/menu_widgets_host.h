// SPDX-License-Identifier: GPL-3.0-or-later
// The menus' selection widgets (src/engine/screens/menu_widgets.h) loaded from gamedata/menu/menu_style.json,
// and the sink that places their draw records in the frame at the original options screen's layers.
#pragma once

#include "engine/screens/menu_widgets.h"

namespace sh::ui {

// Null when the style file does not load (logged once; the widgets then draw nothing).
const sh::screens::MenuWidgets* menu_widgets();
sh::screens::MenuSink& menu_sink();

}  // namespace sh::ui
