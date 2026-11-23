// SPDX-License-Identifier: GPL-3.0-or-later
// Readable, localizable names of every input a control bind can hold: SDL key names
// ("Left Shift", "Keypad 1"), the mouse ("Mouse1", "MouseWheelUp") and SDL gamepad
// buttons and axes ("leftshoulder", "lefttrigger", "dpup"). A name is a list of parts
// joined by a space; each part is a text key of gamedata/text/<lang>/controls_screen.json
// (ui.input.*) with its English, or a literal that is the same in every language
// (letters, digits, "F5", "L1", "LB").
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sh::screens {

struct InputNamePart {
    std::string key;   // text key, empty for a literal
    std::string text;  // English (the fallback when no layer has the key) or the literal
};

using InputName = std::vector<InputNamePart>;

// How the pad's buttons are labelled: the glyph set the screen shows (input/glyphs/glyph_table.h).
enum class PadLabels { Xbox, PlayStation, PlayStationClassic, Nintendo, Steam };

// Which tab of the controls screen a bound value belongs to.
enum class BindDevice { None, Keyboard, Mouse, Pad };

// Every text key the names use, with its English (for the text files and their tests).
struct InputTextKey {
    std::string key;
    std::string english;
};
const std::vector<InputTextKey>& input_text_keys();

bool is_mouse_bind(std::string_view value);  // Mouse1..Mouse5, MouseWheelUp/Down
bool is_wheel_bind(std::string_view value);
// "" / "NONE" is None; a pad column's value is Pad, else Mouse or Keyboard.
BindDevice bind_device(std::string_view value, bool pad_column);

InputName keyboard_input_name(std::string_view sdl_key_name);
InputName mouse_input_name(std::string_view value);
// The name beside a mouse icon: "Left", "Up" (the wheel), "4".
InputName mouse_short_name(std::string_view value);
InputName pad_input_name(std::string_view sdl_pad_name, PadLabels labels);
// dir in up, right, down, left order; dpad false = the left stick.
InputName stick_input_name(int dir, bool dpad);
// Any bound value by its device.
InputName bind_input_name(std::string_view value, bool pad_column, PadLabels labels);

// The English of a name (tests, logs).
std::string english_name(const InputName& name);

}  // namespace sh::screens
