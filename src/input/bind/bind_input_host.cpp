// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "input/bind/bind_input.h"

#include "engine/game_state/game_state.h"
#include "input/mouse_pointer.h"
#include "input/pad.h"

#include <SDL3/SDL.h>

#include <cstring>

namespace sh::input {
namespace {

namespace gs = sh::game_state;

static_assert(SDL_SCANCODE_ESCAPE == 41 && SDL_SCANCODE_CAPSLOCK == 57 && SDL_SCANCODE_SCROLLLOCK == 71 &&
              SDL_SCANCODE_NUMLOCKCLEAR == 83 && SDL_SCANCODE_COUNT == 512,
              "bind_capture.cpp numbers keyboard positions as SDL does");

constexpr int kStickPush = 16000;

BindCapture s_capture;

std::span<const std::uint8_t> keyboard() {
    int count = 0;
    const bool* keys = SDL_GetKeyboardState(&count);
    static_assert(sizeof(bool) == sizeof(std::uint8_t));
    return keys != nullptr ? std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(keys),
                                                           static_cast<std::size_t>(count))
                           : std::span<const std::uint8_t>();
}

bool down(std::span<const std::uint8_t> keys, SDL_Scancode sc) {
    return static_cast<std::size_t>(sc) < keys.size() && keys[sc] != 0;
}

NavState held_nav() {
    NavState h;
    const auto keys = keyboard();
    h[NavKey::Up] = down(keys, SDL_SCANCODE_UP);
    h[NavKey::Down] = down(keys, SDL_SCANCODE_DOWN);
    h[NavKey::Left] = down(keys, SDL_SCANCODE_LEFT);
    h[NavKey::Right] = down(keys, SDL_SCANCODE_RIGHT);
    h[NavKey::Ok] = down(keys, SDL_SCANCODE_RETURN) || down(keys, SDL_SCANCODE_KP_ENTER) || down(keys, SDL_SCANCODE_SPACE);
    h[NavKey::Back] = down(keys, SDL_SCANCODE_ESCAPE);
    h[NavKey::Clear] = down(keys, SDL_SCANCODE_DELETE) || down(keys, SDL_SCANCODE_BACKSPACE);
    h[NavKey::TabPrev] = down(keys, SDL_SCANCODE_Q) || down(keys, SDL_SCANCODE_PAGEUP);
    h[NavKey::TabNext] = down(keys, SDL_SCANCODE_E) || down(keys, SDL_SCANCODE_PAGEDOWN);

    const int stick_x = controller_axis(SDL_GAMEPAD_AXIS_LEFTX);
    const int stick_y = controller_axis(SDL_GAMEPAD_AXIS_LEFTY);
    const char* held = held_bind_name();
    const std::string_view button = held != nullptr ? held : "";
    h[NavKey::Up] |= stick_y < -kStickPush || button == "dpup";
    h[NavKey::Down] |= stick_y > kStickPush || button == "dpdown";
    h[NavKey::Left] |= stick_x < -kStickPush || button == "dpleft";
    h[NavKey::Right] |= stick_x > kStickPush || button == "dpright";
    h[NavKey::Ok] |= button == "a";
    h[NavKey::Back] |= button == "b" || button == "start";
    h[NavKey::Clear] |= button == "x";
    h[NavKey::TabPrev] |= button == "leftshoulder";
    h[NavKey::TabNext] |= button == "rightshoulder";

    // The player's confirm and cancel, as every other menu takes them: Escape is also the default warm reset, so
    // with Escape as the only keyboard way out, leaving the screen reset the game. What the mouse presses through
    // a bind (interact = Mouse1) is left out - the screen reads clicks itself, and one click would act twice.
    std::uint16_t binds[gs::kPadBindCount];
    std::uint32_t pad = 0;
    if (gs::pad_binds(binds) && gs::pad_held(&pad)) {
        pad &= ~mouse_pointer().bound_pad_bits();
        h[NavKey::Ok] |= (pad & (binds[gs::kPadEnter] | binds[gs::kPadAction])) != 0;
        h[NavKey::Back] |= (pad & binds[gs::kPadCancel]) != 0;
    }
    return h;
}

unsigned mouse_button_bits() {
    const SDL_MouseButtonFlags sdl = SDL_GetMouseState(nullptr, nullptr);
    unsigned bits = 0;
    // Config numbering: Mouse1 left, Mouse2 right, Mouse3 middle.
    if (sdl & SDL_BUTTON_LMASK) bits |= 1u << 0;
    if (sdl & SDL_BUTTON_RMASK) bits |= 1u << 1;
    if (sdl & SDL_BUTTON_MMASK) bits |= 1u << 2;
    if (sdl & SDL_BUTTON_X1MASK) bits |= 1u << 3;
    if (sdl & SDL_BUTTON_X2MASK) bits |= 1u << 4;
    return bits;
}

}  // namespace

NavState poll_menu_nav(MenuNav& nav) { return nav.step(held_nav(), static_cast<std::uint32_t>(SDL_GetTicks())); }

bool controller_connected() { return connected_controller_name() != nullptr; }

void begin_bind_capture(int row, BindColumn column, CaptureFilter filter) {
    CaptureTarget target;
    target.pad = is_pad_column(column);
    if (const BindRow* r = BindTable::row(row)) {
        target.mouse = r->mouse;
        target.wheel = r->wheel;
    }
    s_capture.begin(target, filter, static_cast<std::uint32_t>(SDL_GetTicks()));
}

BindCapture::Result update_bind_capture() {
    CaptureInput in;
    in.now_ms = static_cast<std::uint32_t>(SDL_GetTicks());
    in.keys = keyboard();
    in.mouse_buttons = mouse_button_bits();
    // A notch is latched for two frames; the first of them is the notch arriving.
    in.wheel_up = wheel().upFrames == 2;
    in.wheel_down = wheel().downFrames == 2;
    const char* held = held_bind_name();
    in.pad_button = held != nullptr ? held : "";
    in.pad_connected = controller_connected();
    in.key_name = [](int position) {
        const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(position));
        return std::string(name != nullptr ? name : "");
    };
    return s_capture.step(in);
}

std::string key_cap_of(std::string_view saved_name) {
    const std::string name(saved_name);
    const SDL_Scancode sc = SDL_GetScancodeFromName(name.c_str());
    if (sc != SDL_SCANCODE_UNKNOWN) {
        const char* cap = SDL_GetKeyName(SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false));
        if (cap != nullptr && cap[0] != '\0') {
            return cap;
        }
    }
    return name;
}

int position_of_key_cap(std::string_view cap) {
    const std::string text(cap);
    const SDL_Keycode key = SDL_GetKeyFromName(text.c_str());
    if (key != SDLK_UNKNOWN) {
        const SDL_Scancode sc = SDL_GetScancodeFromKey(key, nullptr);
        if (sc != SDL_SCANCODE_UNKNOWN) {
            return static_cast<int>(sc);
        }
    }
    // A character the layout has no unshifted key for (US "`" on QWERTZ) is its US position, as it always was.
    return static_cast<int>(SDL_GetScancodeFromName(text.c_str()));
}

}  // namespace sh::input
