// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The brightness screen's vertical lines: 21 grey lines across the upper half of the screen whose grey follows the
// brightness level, behind everything else of the world table.

#include <cstdint>

#include "engine/config/pc_config.h"
#include "engine/game_state/session.h"
#include "engine/screens/screens_view.h"
#include "engine/world/screen_glow.h"
#include "render/draw2d/draw2d.h"

extern "C" void Options_BrightnessMenu_LinesDraw(std::int32_t level) {
    namespace d2 = sh::draw2d;
    const auto& game = sh::game::session().game;
    const auto grey = static_cast<std::uint8_t>(sh::world::brightness_level_grey(level, g_PcConfig.brightnessLevelBias));
    for (int i = -10; i <= 10; i++) {
        const auto x = static_cast<std::int16_t>(((game.screen_width - 64) / 20) * i);
        d2::Line line;
        line.gouraud = true;
        line.xy[0] = {x, -16};
        line.xy[1] = {x, static_cast<std::int16_t>((game.screen_height / 2) - 45)};
        line.color[0] = {grey, grey, grey};
        line.color[1] = {grey, grey, grey};
        sh::screens::submit_2d(sh::screens::Table2d::World, 1, line);
    }
}
