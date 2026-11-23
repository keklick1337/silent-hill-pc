// SPDX-License-Identifier: GPL-3.0-or-later
// Cutting a line of menu text to a width. The text is in the game font's spelling: UTF-8 with '_' for a space.
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace sh::menu {

// The width of a string, in the units of the limit it is fitted to.
using TextMeasure = std::function<int(const char*)>;

// The longest line the menu text drawer takes in one piece, in bytes.
inline constexpr std::size_t kFitLineBytes = 127;

struct FittedText {
    std::string text;
    bool cut = false;
};

// `text` when it is at most `max_width` wide (and one line long), else its longest beginning that leaves room for
// "...", which is put in place of the rest. A character is never split and the cut never ends in a space.
FittedText fit_with_ellipsis(std::string_view text, int max_width, const TextMeasure& measure);

// Text that is data (a file name, a key's name, a name out of a mod) in the game font's spelling: a space becomes
// '_' (a run of them one), an ASCII character whose byte means something else to the game font ('_' its space,
// '[' a quote, ...) becomes its fullwidth form, control characters go. A character past ASCII stays when
// `font_has` it; one the font lacks becomes its base letter or ASCII look-alike when it has one ("e" for an
// accented e, "..." for an ellipsis), else it stays too, for the font's fallback. The result is at most
// `max_bytes` long, whole characters only.
std::string font_spelling(std::string_view utf8, std::size_t max_bytes, const std::function<bool(char32_t)>& font_has);

// A date and time as the save list shows them: "DD. MM. YYYY. HH:MM" in the game font's spelling.
std::string date_spelling(int day, int month, int year, int hour, int minute);

}  // namespace sh::menu
