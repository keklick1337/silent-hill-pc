// SPDX-License-Identifier: GPL-3.0-or-later
// The input devices as the engine sees them: the per-button binds of keyboard and controllers, the
// connected pads, the mouse wheel latch and the per-frame poll that turns them into the game's pad
// button words (active-low, one bit per PadButton).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sh::input {

// Bits of the game's pad button word.
enum PadButton : std::uint16_t
{
    kButtonSelect = 0x0001,
    kButtonL3 = 0x0002,
    kButtonR3 = 0x0004,
    kButtonStart = 0x0008,
    kButtonUp = 0x0010,
    kButtonRight = 0x0020,
    kButtonDown = 0x0040,
    kButtonLeft = 0x0080,
    kButtonL2 = 0x0100,
    kButtonR2 = 0x0200,
    kButtonL1 = 0x0400,
    kButtonR1 = 0x0800,
    kButtonTriangle = 0x1000,
    kButtonCircle = 0x2000,
    kButtonCross = 0x4000,
    kButtonSquare = 0x8000,
};

// A controller bind is an SDL gamepad button, or an SDL gamepad axis tagged with kBindAxis
// (kBindInverse flips its sign). A negative bind is unbound.
inline constexpr int kBindAxis = 0x4000;
inline constexpr int kBindInverse = 0x8000;
inline constexpr int kUnbound = -1;

// SDL scancode per pad button; 0 (SDL_SCANCODE_UNKNOWN) is unbound.
struct KeyboardBindings
{
    int square = 0, circle = 0, triangle = 0, cross = 0;
    int l1 = 0, l2 = 0, l3 = 0;
    int r1 = 0, r2 = 0, r3 = 0;
    int start = 0, select = 0;
    int dpad_left = 0, dpad_right = 0, dpad_up = 0, dpad_down = 0;
};

// Controller bind per pad button, plus the four stick axes.
struct ControllerBindings
{
    int square = kUnbound, circle = kUnbound, triangle = kUnbound, cross = kUnbound;
    int l1 = kUnbound, l2 = kUnbound, l3 = kUnbound;
    int r1 = kUnbound, r2 = kUnbound, r3 = kUnbound;
    int start = kUnbound, select = kUnbound;
    int dpad_left = kUnbound, dpad_right = kUnbound, dpad_up = kUnbound, dpad_down = kUnbound;
    int axis_left_x = kUnbound, axis_left_y = kUnbound;
    int axis_right_x = kUnbound, axis_right_y = kUnbound;
};

// What moves the player on a pad (config controller_movement); it picks digital or analog mode.
enum class Movement
{
    Stick = 0,
    Dpad = 1,
    Both = 2,
};

struct Bindings
{
    KeyboardBindings keyboard;
    KeyboardBindings keyboardSecondary;  // read only with mouseAndSecondary
    ControllerBindings controller;
    ControllerBindings controllerSecondary;  // a second bind per button; either one presses
    // SDL device index each port accepts; -1 takes any newcomer.
    std::array<int, 2> controllerSlot{-1, -1};
    Movement movement = Movement::Both;
    bool mouseAndSecondary = false;  // the secondary keyboard binds and the mouse buttons are live
    // Pad buttons per mouse slot: SDL buttons 1..5, 6 wheel up, 7 wheel down.
    std::array<std::uint16_t, 8> mouseButtons{};
    bool dpadMovementDisabled = false;  // frees the d-pad for action binds
    std::uint8_t keyboardPorts = 0x1;   // bit n: the keyboard also drives port n
};

Bindings& bindings();

// The stock keyboard and controller binds (the config replaces them once it is read).
void set_default_bindings();

// Name lookups of the config: an SDL scancode name ("RShift"), an SDL gamepad button or axis name
// ("a", "lefttrigger", "-leftx" inverts). "NONE" is unbound; an unknown name gives the fallback.
int lookup_key(const char* name, int fallback);
int lookup_controller_bind(const char* name, int fallback);

// Mouse wheel notches. Each scroll is latched for two frames so the pad word sees a clean press and
// release; the frame end decays the latch, no single reader consumes it.
struct WheelState
{
    int upFrames = 0;
    int downFrames = 0;
    int notches = 0;  // net notches since a reader last cleared it (every notch, unlike the latch)
    int total = 0;    // never cleared: readers diff it against their own last sample

    void scroll(int notchCount);
    void decay();
};

WheelState& wheel();

// The device the player used last: the UI hides its own cursor while it is the pad.
enum class LastDevice
{
    Mouse,
    Pad,
};
void set_last_device(LastDevice device);
LastDevice last_device();

// Text typed while a text field owns the keyboard; a null text is Backspace.
using TextInputHandler = void (*)(const char* text);
void set_text_input_handler(TextInputHandler handler);
TextInputHandler text_input_handler();

// Starts SDL gamepads and adopts the ones already plugged in. False when SDL refuses.
bool init_pads();
void on_pad_added(std::uint32_t instance);
void on_pad_removed(std::uint32_t instance);
// Once per frame: rewrites the game's pad blocks from keyboard, mouse and pads.
void update_pads();
// The per-frame input step of the host: events, then the pads.
void poll();

// Pad bits held by the semantic keyboard and mouse actions (active-low), merged into the keyboard word.
using KeyboardActionSource = std::uint16_t (*)();
void set_keyboard_action_source(KeyboardActionSource source);

// The touch screen (input/touch/touch_controls.h), asked once per pad update before the keyboard: its stick drives
// port 0 in analog mode while `active`, and its skip button counts for skip_held().
struct TouchStick
{
    bool active = false;
    std::uint8_t x = 128, y = 128;
};
using TouchSource = TouchStick (*)();
void set_touch_source(TouchSource source);
using TouchSkip = bool (*)();
void set_touch_skip(TouchSkip skip);

// Physical controller queries, read straight from SDL rather than the merged word, so a keyboard key
// on the same pad button cannot trigger a controller-only action.
bool skip_held();  // the skip of blocking loops (FMV): Cross binds or Start
bool controller_button_held(int sdlButton);
bool controller_bind_held(int bind);
const char* connected_controller_name();  // nullptr when none
const char* held_bind_name();             // a held button or trigger, nullptr when none
void held_bind_names(std::vector<std::string>& out);  // every held button and trigger
int controller_axis(int sdlAxis);         // the pad pushed furthest

} // namespace sh::input
