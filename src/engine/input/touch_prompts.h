// SPDX-License-Identifier: GPL-3.0-or-later
// The game asks the touch screen for an on-screen control it would not show by itself: Harry's script names the
// control and its picture on every tick the moment lasts (sh.input.touch_prompt; a grab asks for "struggle", a downed
// enemy in reach for "attack" with "kick"). Only the touch controls read it; the game's rules never do.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace sh::input {

void note_touch_prompt(std::string_view control, std::string_view icon);
// The icon of the control's prompt when it was asked for within the last `ms` milliseconds ("" when none was named).
std::optional<std::string> touch_prompt(std::string_view control, int ms);

}  // namespace sh::input
