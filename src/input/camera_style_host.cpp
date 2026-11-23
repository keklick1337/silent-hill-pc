// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "input/camera_style.h"

#include "engine/config/pc_config.h"
#include "engine/debug/console_host.h"
#include "engine/devtools/tool_mode.h"
#include "engine/game_state/game_state.h"
#include "engine/log/sh_log.h"
#include "engine/platform/host_state.h"
#include "engine/platform/window.h"
#include "input/mouse_pointer.h"
#include "input/pad.h"
#include "engine/app/control_scheme.h"
#include "platform/sh_platform.h"
#include "ui/menu/pc_debug_menu.h"

#include <SDL3/SDL.h>

#include <span>


// An alternate camera, not the game's, frames the picture (read all over the engine); the free camera flies.
extern int g_DebugThirdPersonCam;
extern int g_DebugCamEnabled;

namespace sh::input {
namespace {

namespace gs = sh::game_state;

std::span<const bool> keyboard() {
    int count = 0;
    const bool* keys = SDL_GetKeyboardState(&count);
    return keys != nullptr ? std::span<const bool>(keys, static_cast<std::size_t>(count)) : std::span<const bool>();
}

bool key_down(std::span<const bool> keys, int scancode) {
    return scancode != SDL_SCANCODE_UNKNOWN && static_cast<std::size_t>(scancode) < keys.size() && keys[scancode];
}

int pad_bind(const char* name) {
    return name[0] != '\0' ? lookup_controller_bind(name, SDL_GAMEPAD_BUTTON_INVALID) : SDL_GAMEPAD_BUTTON_INVALID;
}

// Config numbers the buttons left, right, middle; SDL left, middle, right.
SDL_MouseButtonFlags mouse_mask(int config_button) {
    switch (config_button) {
        case 1: return SDL_BUTTON_LMASK;
        case 2: return SDL_BUTTON_RMASK;
        case 3: return SDL_BUTTON_MMASK;
        case 4: return SDL_BUTTON_X1MASK;
        case 5: return SDL_BUTTON_X2MASK;
        default: return 0;
    }
}

bool free_movement() {
    return gs::game_state() == gs::GameState::InGame && gs::sys_state() == gs::SysState::Gameplay &&
           Pc_ConsoleIsOpen() == 0 && g_PcDebugMenuActive == 0;
}

}  // namespace

void CameraStyles::adopt(CameraStyle style) {
    style_ = style;
    g_DebugThirdPersonCam = is_alternate_camera(style) ? 1 : 0;
    // The binds follow the camera; the input layer has them from the next frame.
    sh::app::apply_camera_control_scheme();
}

void CameraStyles::init() {
    adopt(camera_style_from(g_PcConfig.controlStyle));
    PcConfig_SaveKeyValue("control_styles", camera_style_list().c_str());
    SH_DBG("[CTRLSTYLE] active=%s, %d styles published", camera_style_id(style_), kCameraStyles);
}

void CameraStyles::set(CameraStyle style) {
    const bool was_alternate = g_DebugThirdPersonCam != 0;
    adopt(camera_style_from(static_cast<int>(style)));
    // Coming from the original camera the alternate one starts behind the player; between two alternate cameras
    // the view carries over.
    if (!was_alternate && is_alternate_camera(style_)) {
        camera_reset_ = true;
    }
    PcConfig_SaveKeyValue("control_style", camera_style_id(style_));
}

bool CameraStyles::take_camera_reset() {
    const bool wanted = camera_reset_;
    camera_reset_ = false;
    return wanted;
}

void CameraStyles::reapply_binds() {
    if (menu_binds_) {
        sh::app::apply_classic_control_scheme();
    } else {
        sh::app::apply_camera_control_scheme();
    }
}

void CameraStyles::frame_update() {
    if (resolved_at_ != g_PcBindsGen) {
        resolved_at_ = g_PcBindsGen;
        const ControlScheme* schemes[2] = {&g_PcConfig.classic, &g_PcConfig.altcam};
        for (int i = 0; i < 2; ++i) {
            change_key_[i] = SDL_GetScancodeFromName(schemes[i]->keyChangeCam);
            change_pad_[i] = pad_bind(schemes[i]->padChangeCam);
        }
        swap_mouse_ = mouse_button_of_bind(g_PcConfig.keySwapShoulder);
        swap_key_ = swap_mouse_ == 0 ? SDL_GetScancodeFromName(g_PcConfig.keySwapShoulder) : SDL_SCANCODE_UNKNOWN;
        swap_pad_ = pad_bind(g_PcConfig.padSwapShoulder);
    }

    const bool playing = free_movement();
    const auto keys = keyboard();

    // Change Camera. The controller's bind is read from the controller itself, not from the pad buttons the
    // keyboard also presses: a key bound to the same pad button must not switch the camera.
    {
        const int binds = g_DebugThirdPersonCam != 0 ? 1 : 0;
        const bool key = key_down(keys, change_key_[binds]);
        const bool pad = controller_bind_held(change_pad_[binds]);
        if (playing && ((key && !change_key_was_down_) || (pad && !change_pad_was_down_))) {
            const CameraStyle next = next_camera_style(style_, 1);
            set(next);
            SH_DBG_ECHO("[CTRLSTYLE] Change Camera -> %s", camera_style_id(next));
        }
        change_key_was_down_ = key;
        change_pad_was_down_ = pad;
    }

    // A new game and a loaded one begin with the view at the player, looking where he looks.
    if (gs::game_state() == gs::GameState::MainMenu || gs::game_state() == gs::GameState::MainLoadScreen) {
        camera_reset_ = true;
    }

    // Menus, the pause and the inventory are driven with the original camera's binds: an alternate camera's set
    // (mouse look, other confirm and cancel) is for moving about. Reading a message about something in the world
    // is not a menu - the world only stands still - so the player's own confirm and cancel dismiss it.
    {
        const bool reading = gs::sys_state() == gs::SysState::ReadMessage;
        const bool menu_binds = !playing && !reading && g_DebugThirdPersonCam != 0;
        if (menu_binds != menu_binds_) {
            menu_binds_ = menu_binds;
            reapply_binds();
        }
    }

    // Swap Shoulder: over the shoulder, and third person when it aims over the shoulder (tps_ots_aim).
    {
        bool held = false;
        if (swap_mouse_ != 0) {
            held = (SDL_GetMouseState(nullptr, nullptr) & mouse_mask(swap_mouse_)) != 0;
        } else {
            held = key_down(keys, swap_key_);
        }
        held = held || controller_bind_held(swap_pad_);
        const bool sided = style_ == CameraStyle::OverShoulder ||
                           (style_ == CameraStyle::ThirdPerson && g_PcConfig.tpsOtsAim != 0);
        if (playing && held && !swap_was_down_ && sided) {
            shoulder_side_ = -shoulder_side_;
            SH_DBG("[CTRLSTYLE] OTS shoulder: %s", shoulder_side_ > 0 ? "right" : "left");
        }
        swap_was_down_ = held;
    }

    // Backspace steps the aim mark through its shapes.
    {
        const bool held = key_down(keys, SDL_SCANCODE_BACKSPACE);
        if (playing && held && !crosshair_key_was_down_) {
            const Crosshair next = next_crosshair({g_PcConfig.crosshair != 0, g_PcConfig.crosshairStyle});
            g_PcConfig.crosshair = next.on ? 1 : 0;
            g_PcConfig.crosshairStyle = next.shape;
            SH_DBG_ECHO("[CTRLSTYLE] Crosshair: %s", crosshair_name(next));
        }
        crosshair_key_was_down_ = held;
    }

    // The mouse is captured only while the player moves about with an alternate camera or the free camera flies.
    // Menus, the pause and the console get the pointer, and so does a puzzle with a free cursor whatever the
    // camera: there the mouse moves the puzzle's cursor.
    bool capture = (g_DebugThirdPersonCam != 0 || g_DebugCamEnabled != 0) && playing && !mouse_pointer().puzzle_active();
    // The editor keeps the pointer for its own windows and captures it only while its look button is held.
    if (sh::devtools::tool_mode().active) {
        capture = sh::devtools::tool_mode().mouse_look;
    }
    if (g_window != nullptr && SDL_GetWindowRelativeMouseMode(g_window) != capture) {
        SDL_SetWindowRelativeMouseMode(g_window, capture);
        // What the mouse travelled before the switch must not turn the camera on its first frame.
        Sh_RelativeMouseState(nullptr, nullptr);
        // A released pointer is confined to the window again if the window confines it.
        sh::platform::update_mouse_confinement();
    }
}

CameraStyles& camera_styles() {
    static CameraStyles styles;
    return styles;
}

}  // namespace sh::input
