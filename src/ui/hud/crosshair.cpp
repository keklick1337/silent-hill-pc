// SPDX-License-Identifier: GPL-3.0-or-later
// The crosshair on the running game (crosshair.h).
#include "ui/hud/crosshair.h"

#include "engine/config/pc_config.h"
#include "engine/game_state/game_state.h"
#include "input/camera_style.h"

namespace sh::ui {

namespace {

namespace gs = sh::game_state;

static_assert(CrosshairShape::kMaxQuads <= gs::kOverlayQuadsMax, "the overlay holds every shape");

// In third person a shot leaves Harry's hand, below the camera, and meets the middle of the view only at the
// distance the aim is traced to: nearer shots land on the mark, farther ones a little under it. The mark is drawn
// this many lines low to split that error. It is only where the mark is drawn; where a bullet goes does not change.
constexpr int kThirdPersonDrop = 4;

}  // namespace

void draw_crosshair() {
    const sh::input::CameraStyle style = sh::input::camera_styles().style();
    const bool third_person = style == sh::input::CameraStyle::ThirdPerson || style == sh::input::CameraStyle::OverShoulder;
    const bool first_person = style == sh::input::CameraStyle::FirstPerson;

    CrosshairView view;
    view.enabled = g_PcConfig.crosshair != 0;
    view.aimed_camera = third_person || first_person;
    if (!view.enabled || !view.aimed_camera) {
        return;
    }
    view.in_play = gs::game_state() == gs::GameState::InGame && gs::sys_state() == gs::SysState::Gameplay;
    if (!view.in_play) {
        return;
    }
    view.aiming = gs::player_aiming();
    view.only_while_aiming = g_PcConfig.crosshairFpsAimOnly != 0;
    view.hide_without_gun = g_PcConfig.crosshairHideMelee != 0;
    // The attacks of the guns come after those of the melee weapons and the bare hands.
    view.gun_in_hand = gs::player_weapon_attack() >= gs::kFirstGunAttack;
    if (!crosshair_visible(view)) {
        return;
    }

    // First person aims down the view itself: no drop.
    const CrosshairShape shape(g_PcConfig.crosshairStyle, g_PcConfig.crosshairSize, third_person ? kThirdPersonDrop : 0);
    gs::OverlayQuad quads[CrosshairShape::kMaxQuads];
    for (int i = 0; i < shape.count(); i++) {
        for (int corner = 0; corner < 4; corner++) {
            quads[i].x[corner] = shape.quad(i).x[corner];
            quads[i].y[corner] = shape.quad(i).y[corner];
        }
    }
    gs::overlay_translucent_quads(quads, shape.count(), 255, 255, 255);
}

}  // namespace sh::ui
