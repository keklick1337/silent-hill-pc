// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The inventory screen's layout on the window this frame (wide_layout.h): spread over a window wider than 4:3 when
// the menus use the whole width (menu_pillarbox 1) and the picture is not kept 4:3 (widescreen_mode 1), the original
// 4:3 layout otherwise.

#pragma once

#include "engine/inventory/wide_layout.h"

namespace sh::inventory {

WideLayout wide_screen();

}  // namespace sh::inventory
