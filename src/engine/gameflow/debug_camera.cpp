// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/gameflow/debug_camera.h"

namespace sh::gameflow {

namespace {

// The third-person camera is a normal (non-debug) camera: it stands whenever the free camera is off.
void apply_third_person_if_active(DebugCameraHost& h) {
    if (h.alt_camera_state_ok() && !h.free_camera_on() && h.third_person_on()) h.third_person_apply();
}

}  // namespace

void debug_camera_update(DebugCameraHost& h) {
    if (!h.keyboard_ready()) return;

    if (!h.dev_controls_allowed()) {
        // The free camera is a user feature, so it flies with the dev keys off too.
        if (h.free_camera_on() && h.in_game()) {
            h.free_camera_fly();
            h.free_camera_apply();
            return;
        }
        apply_third_person_if_active(h);
        return;
    }

    if (h.console_or_debug_menu_open()) {
        // Hold the free camera's view under the panel.
        if (h.free_camera_on() && h.in_game()) {
            h.free_camera_apply();
            return;
        }
        // The panel is open but the game left InGame: end the free camera properly.
        if (h.free_camera_on()) h.free_camera_set(false);
        apply_third_person_if_active(h);
        return;
    }

    if (!h.in_game()) {
        if (h.free_camera_on()) h.free_camera_set(false);
        apply_third_person_if_active(h);
        return;
    }

    h.dev_keys_update();

    if (!h.free_camera_on()) {
        if (h.third_person_on()) h.third_person_apply();
        return;
    }

    h.free_camera_fly();
    h.free_camera_apply();
}

}  // namespace sh::gameflow
