// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Controller binds that are a combination of two buttons ("leftshoulder+rightshoulder"): the value as config.cfg
// writes it, and the per-frame resolver that tells a combination from its buttons pressed alone. The resolver needs
// neither SDL nor the game: the caller names the buttons by number and says which are held.
//
// A combination fires when its second button goes down while the first is held, the two presses at most
// kChordWindowMs apart. While it is held, its buttons do not fire their own binds; a button that belongs to a
// combination waits up to kChordWindowMs before its own bind fires, so pressing L1 a little before R1 is the
// combination, not L1's bind and then the combination.

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::input {

// The original remembers a press of each side step button for six controller calls at 30 Hz, and turns when both
// are remembered: the second press may come five ticks (167 ms) after the first.
inline constexpr std::uint32_t kChordWindowMs = 167;
inline constexpr std::size_t kChordMaxButtons = 2;
inline constexpr char kChordSeparator = '+';

// The button names of a bind value, trimmed: one for a plain bind, two for a combination, none for "" / NONE. Parts
// past the second are dropped.
std::vector<std::string> chord_parts(std::string_view value);
// The value of a combination of these names ("a+b"); "NONE" when there are none.
std::string chord_value(std::span<const std::string> parts);
bool is_chord(std::string_view value);

// One bind as button numbers; a negative number is a button that is not there.
struct PadBind {
    std::array<int, kChordMaxButtons> buttons{-1, -1};
    int count = 0;

    bool chord() const { return count == 2; }
    bool bound() const { return count > 0; }
};

// `lookup` gives a name's button number, negative for an unknown name. A combination with an unknown or repeated
// button is unbound, so a typo never leaves half a combination firing alone.
PadBind parse_pad_bind(std::string_view value, const std::function<int(const std::string&)>& lookup);

class ChordResolver {
public:
    void set_binds(std::vector<PadBind> binds);
    // Forgets what was held (a change of binds or of control scheme).
    void reset();
    // One frame: `held` says whether a button number is down.
    void step(std::uint32_t now_ms, const std::function<bool(int)>& held);

    // The bind went off this frame.
    bool clicked(std::size_t bind) const;
    // The bind is held: a combination both of its buttons since it fired, a single button once its own press
    // counted (not taken by a combination, not waiting for one).
    bool held(std::size_t bind) const;

private:
    struct Button {
        int id = -1;
        bool down = false;
        bool pressed = false;  // went down this frame
        std::uint32_t press_ms = 0;
    };
    struct State {
        bool clicked = false;
        bool active = false;   // a combination that fired and is still held
        bool pending = false;  // a single button waiting to see whether a combination takes its press
        bool taken = false;    // a single button whose press a combination took; until it is released
        std::uint32_t since_ms = 0;
    };

    Button* button(int id);
    const Button* button(int id) const;
    // A combination with `id` among its buttons is held, or could still be completed by a press within the window.
    bool chord_active(int id) const;
    bool chord_possible(int id, std::uint32_t now_ms) const;

    std::vector<PadBind> binds_;
    std::vector<State> state_;
    std::vector<Button> buttons_;
};

}  // namespace sh::input
