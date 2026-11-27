// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/app/control_scheme.h"

#include <SDL3/SDL.h>

#include "engine/config/pc_config.h"
#include "input/pad.h"
#include "input/pc_actions.h"

// The developer and cheat keys work only with this on (config allow_debug_controls); read all over the engine.
int g_PcAllowDebugControls = 0;

// An alternate camera, not the game's, frames the picture.
extern int g_DebugThirdPersonCam;

namespace sh::app {

namespace {

using sh::input::ControllerBindings;

// A pad button of the game and the config's two names for what presses it (the second is the spare assignment).
struct ButtonNames {
    int ControllerBindings::* button;
    char (ControlScheme::* first)[PC_BIND_SLOT_LEN];
    char (ControlScheme::* spare)[PC_BIND_SLOT_LEN];
};

constexpr ButtonNames kButtons[] = {
    {&ControllerBindings::cross, &ControlScheme::padCross, &ControlScheme::padCross2},
    {&ControllerBindings::circle, &ControlScheme::padCircle, &ControlScheme::padCircle2},
    {&ControllerBindings::triangle, &ControlScheme::padTriangle, &ControlScheme::padTriangle2},
    {&ControllerBindings::square, &ControlScheme::padSquare, &ControlScheme::padSquare2},
    {&ControllerBindings::l1, &ControlScheme::padL1, &ControlScheme::padL12},
    {&ControllerBindings::r1, &ControlScheme::padR1, &ControlScheme::padR12},
    {&ControllerBindings::l2, &ControlScheme::padL2, &ControlScheme::padL22},
    {&ControllerBindings::r2, &ControlScheme::padR2, &ControlScheme::padR22},
    {&ControllerBindings::l3, &ControlScheme::padL3, &ControlScheme::padL32},
    {&ControllerBindings::r3, &ControlScheme::padR3, &ControlScheme::padR32},
    {&ControllerBindings::start, &ControlScheme::padStart, &ControlScheme::padStart2},
    {&ControllerBindings::select, &ControlScheme::padSelect, &ControlScheme::padSelect2},
};

// A name the config does not know, "NONE" included, leaves the button unassigned: nothing falls back to a
// built-in choice, so the config is the whole truth.
int button_named(const char* name) { return sh::input::lookup_controller_bind(name, SDL_GAMEPAD_BUTTON_INVALID); }

void apply(const ControlScheme& scheme, bool alternate_camera) {
    sh::input::Bindings& binds = sh::input::bindings();

    // The keyboard and the mouse reach the game as named actions, not through the
    // per-button tables, which stay empty.
    binds.mouseButtons = {};
    binds.keyboard = {};
    binds.keyboardSecondary = {};
    PcActions_SetScheme(alternate_camera);

    // The d-pad and the sticks keep what the platform start gave them; only the buttons are the config's.
    for (const ButtonNames& names : kButtons) {
        binds.controller.*names.button = button_named(scheme.*names.first);
        binds.controllerSecondary.*names.button = button_named(scheme.*names.spare);
    }

    g_PcAllowDebugControls = g_PcConfig.allowDebugControls;
    binds.movement = static_cast<sh::input::Movement>(g_PcConfig.controllerMovement);
    binds.mouseAndSecondary = true;
}

}  // namespace

void apply_camera_control_scheme() {
    const bool alternate = g_DebugThirdPersonCam != 0;
    apply(alternate ? g_PcConfig.altcam : g_PcConfig.classic, alternate);
}

void apply_classic_control_scheme() { apply(g_PcConfig.classic, false); }

}  // namespace sh::app
