// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/inventory/inventory_draw.h"

#include "engine/inventory/inventory.h"
#include "engine/inventory/inventory_menu.h"
#include "engine/inventory/screen_data.h"

namespace sh::inventory {

CommandColumn layout_command_column(std::uint8_t command, bool on_off_hidden, bool active, int row)
{
    return layout_command_column(default_screen_data(), command, on_off_hidden, active, row);
}

std::array<GradientTriangle, 4> scroll_arrows(std::int32_t frame)
{
    return scroll_arrows(default_screen_data(), frame);
}

}  // namespace sh::inventory
