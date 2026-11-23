// SPDX-License-Identifier: GPL-3.0-or-later
#include "features/freecam/controls_panel.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "engine/devtools/tool_mode.h"
#include "engine/game_state/game_state.h"
#include "features/hud/text_panel.h"
#include "render/overlay/overlay.h"

// The free camera's state as its host mirrors it (engine/app/devtools/free_camera_host.cpp), and the two layers
// that draw over the same corner.
extern int g_DebugCamEnabled;
extern int g_PcFreeCamHudHidden;
extern int g_PcFreeCamSteady;
extern int g_PcFreeCamCruise;
extern float g_PcFreeCamSpeedMul;
extern int g_PcDebugMenuActive;
extern int g_PcConsoleInputActive;

namespace sh::freecam {

namespace {

bool panel_wanted() {
    return g_DebugCamEnabled != 0 && g_PcFreeCamHudHidden == 0 && !sh::devtools::tool_mode().active &&
           sh::game_state::game_state() == sh::game_state::GameState::InGame && g_PcDebugMenuActive == 0 &&
           g_PcConsoleInputActive == 0;
}

std::string heading() {
    char text[64];
    std::snprintf(text, sizeof(text), "FREE CAMERA      speed x%.2f%s", static_cast<double>(g_PcFreeCamSpeedMul),
                  g_PcFreeCamSteady != 0 ? "   STEADY" : "");
    return text;
}

}  // namespace

void draw_controls_panel() {
    if (!panel_wanted() || !sh::hud::shared_font().ready()) {
        return;
    }
    const sh::overlay::Overlay& overlay = sh::overlay::overlay();
    if (!overlay.frame().valid()) {
        return;
    }
    const float picture_height = overlay.height();

    const std::string rows[] = {
        heading(),
        "W A S D\tmove",
        "E / Q\tup / down",
        "Mouse\tlook",
        "Wheel\tchange speed",
        "Shift / Ctrl\tfast / slow (hold)",
        "Space\tsteady: straight line, slow and smooth (hold)",
        std::string(",\tcruise control: ") + (g_PcFreeCamCruise != 0 ? "ON" : "off"),
        "H\thide this panel",
    };

    static sh::hud::TextPanel panel;
    if (!panel.set(rows, std::max(static_cast<int>(0.022f * picture_height), 12))) {
        return;
    }
    // The bottom left corner, clear of the edge.
    const float margin = 0.03f * picture_height;
    const float padding = 0.012f * picture_height;
    panel.draw({margin, picture_height - margin - panel.box_height(padding)}, padding);
}

}  // namespace sh::freecam
