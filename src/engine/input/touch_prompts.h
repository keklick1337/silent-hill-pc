// SPDX-License-Identifier: GPL-3.0-or-later
// The game asks the touch screen for an on-screen control it would not show by itself: Harry's script names the
// control and its picture on every tick the moment lasts (sh.input.touch_prompt; a grab asks for "struggle", a downed
// enemy in reach for "attack" with "kick"). Only the touch controls read it; the game's rules never do.
// A menu asks the same way for a button of its own (the save list's "delete"): the touch controls show it, and its
// tap comes back as a press the menu takes once.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace sh::input {

void note_touch_prompt(std::string_view control, std::string_view icon);
// The icon of the control's prompt when it was asked for within the last `ms` milliseconds ("" when none was named).
std::optional<std::string> touch_prompt(std::string_view control, int ms);

// The save list's Delete: the button, and a long press on a row while the button shows.
inline constexpr std::string_view kTouchDelete = "delete";

// A press of a control the touch controls showed for a prompt.
void note_touch_prompt_press(std::string_view control);
// True once for a press noted within the last `ms` milliseconds; the press is gone after it (or when too old).
bool take_touch_prompt_press(std::string_view control, int ms);

}  // namespace sh::input
