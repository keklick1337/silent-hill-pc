// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// End-of-frame clear-colour policy and the item-pass projection exemption of the main loop.
// Pure rules:
// the bridge fills ClearColorIn from the game state and applies ClearColorOut.

#pragma once

namespace sh::gameflow {

struct ClearColorIn {
    bool map_screen_active = false;
    bool present_last_frame = false;   // a frozen foggy frame is presented behind a message
    bool in_game = false;
    bool inventory_screen = false;
    bool cutscene_active = false;      // SysFlag_CutsceneActive
    bool alessa_shop_scene = false;    // the one map whose cutscene wants a black void
    bool game_over_state = false;
    bool world_drawn = false;
    bool fog_enabled = false;
    bool bg2d_held = false;
};

enum class ClearColor { Keep, Black, Fog };

struct ClearColorOut {
    ClearColor color = ClearColor::Keep;
    bool set_fog_uniform = false;      // copy the world fog colour to the renderer's fog colour
};

ClearColorOut clear_color_choose(const ClearColorIn& in);

// 2D-only states suppress dithering; the in-game state (world and cutscenes) keeps it.
bool dither_suppressed(bool in_game);

// The pickup screen projects with a zero anchor while the world is not being drawn.
bool pickup_zero_anchor(bool in_game, bool pickup_active, bool world_drawn, bool cutscene_active,
                        bool cutscene_border_active);

}  // namespace sh::gameflow
