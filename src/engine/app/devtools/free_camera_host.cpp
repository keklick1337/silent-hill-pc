// SPDX-License-Identifier: GPL-3.0-or-later
//
// The free camera on the session (sh::devtools::FreeCamera): reads the keyboard, the
// mouse and the first pad into FreeCameraInput, starts from the game camera and hands its view to the game's view.
// The controls panel (src/features/freecam/controls_panel.cpp) reads the camera's mode through the g_PcFreeCam*
// mirrors; the Pc_FreeCam_* names are what the console, the quick options and the dev keys call.

#include <algorithm>
#include <array>
#include <cstdint>

#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_timer.h>

#include "engine/app/game_view.h"
#include "engine/camera/precise_view.h"
#include "engine/camera/view_space.h"
#include "input/pad.h"
#include "engine/config/pc_config.h"
#include "engine/devtools/free_camera.h"
#include "engine/devtools/tool_mode.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/log/sh_log.h"
#include "engine/loop/frame_view.h"
#include "platform/sh_platform.h"

extern int g_DebugCamEnabled;
extern int g_DebugFogDisabled;
#include "features/hud/text_panel.h"

extern "C" {
extern const unsigned char* g_sdlKeyboardState;
}

int g_PcFreeCamHudHidden = 0;
int g_PcFreeCamSteady = 0;
int g_PcFreeCamCruise = 0;
float g_PcFreeCamSpeedMul = 1.0f;

namespace {

namespace dt = sh::devtools;

constexpr std::uint32_t kPadL2 = 1u << 8, kPadR2 = 1u << 9, kPadL1 = 1u << 10, kPadR1 = 1u << 11;

dt::FreeCamera s_camera;
std::uint64_t s_last_fly_ns = 0;

// The editor holds the game, whose frame step is then 0: the camera flies on the wall clock instead.
std::int32_t wall_clock_step() {
    const std::uint64_t now = SDL_GetTicksNS();
    const std::uint64_t last = s_last_fly_ns;
    s_last_fly_ns = now;
    if (last == 0 || now <= last) return 0;
    const double seconds = std::min(static_cast<double>(now - last) / 1e9, 0.1);  // a stall is not a jump
    return static_cast<std::int32_t>(seconds * 4096.0);
}

void mirror()
{
    g_PcFreeCamHudHidden = s_camera.hud_hidden ? 1 : 0;
    g_PcFreeCamSteady = s_camera.steady ? 1 : 0;
    g_PcFreeCamCruise = s_camera.cruise ? 1 : 0;
    g_PcFreeCamSpeedMul = s_camera.speed_scale;
}

dt::FreeCameraInput read_input()
{
    const unsigned char* ks = g_sdlKeyboardState;
    dt::FreeCameraInput in;
    in.delta_time = sh::loop::frame_delta();
    const std::int32_t wall = wall_clock_step();
    if (sh::devtools::tool_mode().active) in.delta_time = wall;
    in.keys.hud = ks[SDL_SCANCODE_H] != 0;
    in.keys.cruise = ks[SDL_SCANCODE_COMMA] != 0;
    in.keys.steady = ks[SDL_SCANCODE_SPACE] != 0;
    in.keys.fast = ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT];
    in.keys.slow = ks[SDL_SCANCODE_LCTRL] != 0;
    in.keys.forward = ks[SDL_SCANCODE_W] != 0;
    in.keys.back = ks[SDL_SCANCODE_S] != 0;
    in.keys.left = ks[SDL_SCANCODE_A] != 0;
    in.keys.right = ks[SDL_SCANCODE_D] != 0;
    in.keys.sink = ks[SDL_SCANCODE_Q] || ks[SDL_SCANCODE_C];
    in.keys.rise = ks[SDL_SCANCODE_E] != 0;

    in.wheel_notches = sh::input::wheel().notches;
    sh::input::wheel().notches = 0;
    Sh_RelativeMouseState(&in.mouse_dx, &in.mouse_dy);
    // The editor: the pointer turns the camera only while its look button is held, the keys
    // and the wheel stay with the UI while it takes them.
    const sh::devtools::ToolMode& tool = sh::devtools::tool_mode();
    if (tool.active) {
        if (!tool.mouse_look) in.mouse_dx = in.mouse_dy = 0;
        if (tool.wheel_captured) in.wheel_notches = 0;
        if (tool.keyboard_captured) in.keys = {};
        in.keys.hud = false;
    }
    in.mouse_sensitivity = g_PcConfig.mouseSensitivity;
    in.invert_mouse_y = g_PcConfig.invertMouseY != 0;

    const sh::game::PadState& pad = sh::game::session().game.pads[0];
    in.pad.present = true;
    in.pad.right_x = pad.raw.right_x;
    in.pad.right_y = pad.raw.right_y;
    in.pad.left_x = pad.raw.left_x;
    in.pad.left_y = pad.raw.left_y;
    in.pad.fast = (pad.held & kPadR1) != 0;
    in.pad.slow = (pad.held & kPadL1) != 0;
    in.pad.rise = (pad.held & kPadR2) != 0;
    in.pad.sink = (pad.held & kPadL2) != 0;
    in.pad_sensitivity = g_PcConfig.controllerSensitivity;
    in.invert_pad_y = g_PcConfig.invertControllerY != 0;
    return in;
}

}  // namespace

extern "C" void Pc_FreeCam_Set(int on)
{
    on = on ? 1 : 0;
    if (on == g_DebugCamEnabled) return;
    g_DebugCamEnabled = on;
    if (on) {
        std::int32_t pos[3];
        sh::game_state::camera_position(pos);
        s_camera.start({pos[0], pos[1], pos[2]}, sh::game::session().system.camera_angle_y);
        mirror();
        sh::input::wheel().notches = 0;
        sh::hud::load_shared_font();
        Sh_RelativeMouseState(nullptr, nullptr);  // drop the travel from before the capture
        // Fog off: outdoors the fog wall sits a few units past Harry, so a camera that flies anywhere useful would
        // see only fog colour. Numpad . brings it back.
        g_DebugFogDisabled = 1;
        SH_DBG_ECHO("[FREECAM] on -- controls panel bottom-left, H hides it");
    } else {
        sh::camera::clear_free_camera_view();
        sh::app::view::player_show();
        SH_DBG_ECHO("[FREECAM] off");
    }
}

extern "C" void Pc_FreeCam_Apply(void)
{
    const std::array<std::int32_t, 3> at = s_camera.look_at();
    // The eye the view system is about to be handed, for the renderer's unrounded camera: a slow turn or pan is drawn
    // without the whole picture stepping by angle units. Told first: the view system publishes the frame's camera
    // while it takes the look-at, and must find this frame's eye, not the one before.
    const sh::camera::Vec3 eye = {s_camera.position[0], s_camera.position[1], s_camera.position[2]};
    const sh::camera::Vec3 target = {at[0], at[1], at[2]};
    const std::array<double, 3> exact = s_camera.exact_position();
    sh::camera::set_free_camera_view(sh::camera::look_at_matrix(eye, target), s_camera.exact_yaw(),
                                     s_camera.exact_pitch(), exact.data());
    sh::app::view::view_look_at(s_camera.position.data(), at.data());
}

extern "C" void Pc_FreeCam_Fly(void)
{
    if (g_sdlKeyboardState == nullptr) return;
    s_camera.update(read_input());
    mirror();
}

// The camera put at a Q12 world point facing `yaw` (Q12 turns), level (the editor's teleport).
extern "C" void Pc_FreeCam_Place(const std::int32_t* position, std::int32_t yaw)
{
    s_camera.start({position[0], position[1], position[2]}, yaw);
    mirror();
}

// The heading and pitch in angle units (4096 a turn, pitch positive down), fractions kept.
extern "C" void Pc_FreeCam_Aim(double yaw, double pitch)
{
    s_camera.aim(yaw, pitch);
    mirror();
}

// The same with their fractions (angle units).
extern "C" void Pc_FreeCam_ExactAngles(double* yaw, double* pitch)
{
    *yaw = s_camera.exact_yaw();
    *pitch = s_camera.exact_pitch();
}

// The camera's heading and pitch (Q12 turns).
extern "C" void Pc_FreeCam_Angles(std::int32_t* yaw, std::int32_t* pitch)
{
    *yaw = s_camera.yaw;
    *pitch = s_camera.pitch;
}

// Three Q12 coordinates (the game's VECTOR3).
extern "C" void Pc_FreeCam_Position(std::int32_t* out)
{
    out[0] = s_camera.position[0];
    out[1] = s_camera.position[1];
    out[2] = s_camera.position[2];
}
