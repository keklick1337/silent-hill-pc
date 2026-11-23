// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/gameflow/clear_color.h"

namespace sh::gameflow {

ClearColorOut clear_color_choose(const ClearColorIn& in) {
    ClearColorOut out;
    // Menus clear to black; stating it avoids inheriting the gameplay fog colour for a frame.
    if (in.map_screen_active && !in.present_last_frame) {
        out.color = ClearColor::Black;
    } else if (in.inventory_screen) {
        out.color = ClearColor::Black;
    } else if (in.in_game && in.cutscene_active && in.alessa_shop_scene) {
        out.color = ClearColor::Black;
        out.set_fog_uniform = true;
    } else if (in.in_game && in.game_over_state && !in.world_drawn) {
        out.color = ClearColor::Black;
    } else if (in.in_game && in.fog_enabled) {
        // Fog is the sky behind a drawn world only; flat 2D background screens stay black.
        out.color = (!in.bg2d_held && (in.world_drawn || in.present_last_frame)) ? ClearColor::Fog
                                                                                   : ClearColor::Black;
        out.set_fog_uniform = true;
    }
    return out;
}

bool dither_suppressed(bool in_game) { return !in_game; }

bool pickup_zero_anchor(bool in_game, bool pickup_active, bool world_drawn, bool cutscene_active,
                        bool cutscene_border_active) {
    return in_game && pickup_active && !world_drawn && !cutscene_active && !cutscene_border_active;
}

}  // namespace sh::gameflow
