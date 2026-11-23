// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The two input loops of a rebinding screen, as plain state machines over a snapshot of the devices: menu
// navigation with key repeat, and "press the input you want" capture. The screen reads the devices itself
// (bind_input.h) so that it keeps working whatever the game's own binds are - a bad bind cannot lock the player
// out of the screen that fixes it.

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace sh::input {

enum class NavKey : int { Up = 0, Down, Left, Right, Ok, Back, Clear, TabPrev, TabNext };
inline constexpr int kNavKeys = 9;

struct NavState {
    std::array<bool, kNavKeys> on{};

    bool operator[](NavKey k) const { return on[static_cast<std::size_t>(k)]; }
    bool& operator[](NavKey k) { return on[static_cast<std::size_t>(k)]; }
};

// Turns held navigation inputs into presses; the four directions repeat while held.
class MenuNav {
public:
    static constexpr std::uint32_t kRepeatDelayMs = 360;
    static constexpr std::uint32_t kRepeatEveryMs = 90;

    MenuNav() { reset(); }
    // Everything counts as held until seen released: the press that opened the screen is not a press in it.
    void reset();
    NavState step(const NavState& held, std::uint32_t now_ms);

private:
    std::array<bool, kNavKeys> was_held_{};
    std::array<std::uint32_t, kNavKeys> repeat_at_{};
};

// Which devices a keyboard cell's capture listens to.
enum class CaptureFilter { KeysAndMouse, KeysOnly, MouseOnly };

// The cell being rebound.
struct CaptureTarget {
    bool pad = false;    // a controller cell
    bool mouse = false;  // a keyboard cell that takes mouse buttons
    bool wheel = false;  // ...and the wheel
};

// One frame of the devices.
struct CaptureInput {
    std::uint32_t now_ms = 0;
    std::span<const std::uint8_t> keys;  // by keyboard position (USB usage number); nonzero = held
    unsigned mouse_buttons = 0;          // bit n-1 = MouseN, n in 1..5
    bool wheel_up = false;               // a notch arrived this frame
    bool wheel_down = false;
    std::string_view pad_button;         // name of a held controller input, "" for none
    bool pad_connected = false;
    // Config name of a keyboard position, "" when it has none.
    std::function<std::string(int position)> key_name;
};

class BindCapture {
public:
    static constexpr std::uint32_t kPadTimeoutMs = 8000;

    enum class Status { Waiting, Cancelled, Captured };
    struct Result {
        Status status = Status::Waiting;
        std::string name;  // the bind name when Captured
    };

    void begin(const CaptureTarget& target, CaptureFilter filter, std::uint32_t now_ms);
    Result step(const CaptureInput& in);

private:
    static constexpr std::size_t kPositions = 512;

    Result step_pad(const CaptureInput& in);
    Result step_keys(const CaptureInput& in);

    CaptureTarget target_;
    CaptureFilter filter_ = CaptureFilter::KeysAndMouse;
    std::uint32_t started_ms_ = 0;
    bool armed_ = false;  // every input has been let go since the capture began
    std::array<std::uint8_t, kPositions> last_keys_{};
};

}  // namespace sh::input
