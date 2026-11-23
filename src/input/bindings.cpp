// SPDX-License-Identifier: GPL-3.0-or-later
// The bind tables the pad words are read through, their stock values, the name lookups the config
// uses to turn "RShift" or "-leftx" into them, and the small input state the event pump writes.
#include "input/pad.h"

#include <SDL3/SDL.h>

namespace sh::input {
namespace {

Bindings s_bindings;
WheelState s_wheel;
LastDevice s_lastDevice = LastDevice::Mouse;
TextInputHandler s_textInput = nullptr;

} // namespace

Bindings& bindings() { return s_bindings; }

void set_default_bindings()
{
    KeyboardBindings& kb = s_bindings.keyboard;
    kb.square = SDL_SCANCODE_X;
    kb.circle = SDL_SCANCODE_V;
    kb.triangle = SDL_SCANCODE_Z;
    kb.cross = SDL_SCANCODE_C;

    // Sidestep on A/D, aim and view on the two shifts. Right Ctrl never reached the pad word on
    // some Windows setups, so the control keys carry nothing.
    kb.l1 = SDL_SCANCODE_A;
    kb.l2 = SDL_SCANCODE_RSHIFT;
    kb.l3 = SDL_SCANCODE_UNKNOWN;
    kb.r1 = SDL_SCANCODE_D;
    kb.r2 = SDL_SCANCODE_LSHIFT;
    kb.r3 = SDL_SCANCODE_UNKNOWN;

    kb.dpad_up = SDL_SCANCODE_UP;
    kb.dpad_down = SDL_SCANCODE_DOWN;
    kb.dpad_left = SDL_SCANCODE_LEFT;
    kb.dpad_right = SDL_SCANCODE_RIGHT;

    kb.select = SDL_SCANCODE_SPACE;
    kb.start = SDL_SCANCODE_RETURN;

    ControllerBindings& gp = s_bindings.controller;
    gp.square = SDL_GAMEPAD_BUTTON_WEST;
    gp.circle = SDL_GAMEPAD_BUTTON_EAST;
    gp.triangle = SDL_GAMEPAD_BUTTON_NORTH;
    gp.cross = SDL_GAMEPAD_BUTTON_SOUTH;

    gp.l1 = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    gp.l2 = SDL_GAMEPAD_AXIS_LEFT_TRIGGER | kBindAxis;
    gp.l3 = SDL_GAMEPAD_BUTTON_LEFT_STICK;
    gp.r1 = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    gp.r2 = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER | kBindAxis;
    gp.r3 = SDL_GAMEPAD_BUTTON_RIGHT_STICK;

    gp.dpad_up = SDL_GAMEPAD_BUTTON_DPAD_UP;
    gp.dpad_down = SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    gp.dpad_left = SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    gp.dpad_right = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;

    gp.select = SDL_GAMEPAD_BUTTON_BACK;
    gp.start = SDL_GAMEPAD_BUTTON_START;

    gp.axis_left_x = SDL_GAMEPAD_AXIS_LEFTX | kBindAxis;
    gp.axis_left_y = SDL_GAMEPAD_AXIS_LEFTY | kBindAxis;
    gp.axis_right_x = SDL_GAMEPAD_AXIS_RIGHTX | kBindAxis;
    gp.axis_right_y = SDL_GAMEPAD_AXIS_RIGHTY | kBindAxis;

    // Secondary pad binds start unset: anything the game's control config does not fill reads as
    // "not pressed".
    s_bindings.controllerSecondary = ControllerBindings{};
}

int lookup_key(const char* name, int fallback)
{
    if (!name)
        return fallback;
    if (SDL_strcasecmp(name, "NONE") == 0)
        return SDL_SCANCODE_UNKNOWN;

    for (int code = 0; code < SDL_SCANCODE_COUNT; ++code)
    {
        const char* known = SDL_GetScancodeName(static_cast<SDL_Scancode>(code));
        if (known[0] != '\0' && SDL_strcasecmp(known, name) == 0)
            return code;
    }
    return fallback;
}

// Names are SDL's gamepad strings: buttons (a b x y back guide start leftstick rightstick
// leftshoulder rightshoulder dpup dpdown dpleft dpright) and axes (leftx lefty rightx righty
// lefttrigger righttrigger).
int lookup_controller_bind(const char* name, int fallback)
{
    if (!name)
        return fallback;
    if (SDL_strcasecmp(name, "NONE") == 0)
        return SDL_GAMEPAD_BUTTON_INVALID;

    for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button)
    {
        const char* known = SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(button));
        if (known && known[0] != '\0' && SDL_strcasecmp(known, name) == 0)
            return button;
    }

    int flags = kBindAxis;
    const char* axisName = name;
    if (*axisName == '-')
    {
        flags |= kBindInverse;
        ++axisName;
    }
    for (int axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT; ++axis)
    {
        const char* known = SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(axis));
        if (known && known[0] != '\0' && SDL_strcasecmp(known, axisName) == 0)
            return axis | flags;
    }
    return fallback;
}

void WheelState::scroll(int notchCount)
{
    notches += notchCount;
    total += notchCount;
    if (notchCount > 0)
        upFrames = 2;
    else if (notchCount < 0)
        downFrames = 2;
}

void WheelState::decay()
{
    if (upFrames > 0)
        --upFrames;
    if (downFrames > 0)
        --downFrames;
}

WheelState& wheel() { return s_wheel; }

void set_last_device(LastDevice device) { s_lastDevice = device; }
LastDevice last_device() { return s_lastDevice; }

void set_text_input_handler(TextInputHandler handler) { s_textInput = handler; }
TextInputHandler text_input_handler() { return s_textInput; }

} // namespace sh::input
