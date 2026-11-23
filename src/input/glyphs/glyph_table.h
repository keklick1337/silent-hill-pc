// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Which picture of the input prompt sheet stands for a bind: a key cap, a mouse button, or a controller button
// in the lettering of the controller family in use. Tables only - no SDL, no renderer; the sheet itself is loaded
// by glyph_sheet.h. The sheet and the cell list are built by tools/scripts/build_input_glyphs.py.

#pragma once

#include <string_view>

namespace sh::input {

enum class Glyph : int {
    None = -1,
#define SH_INPUT_GLYPH(name, column, row, cells) name,
#include "input/glyphs/glyph_cells.inc"
#undef SH_INPUT_GLYPH
    Count
};

// Controller lettering. The numbers are the values of the config key gamepad_icons.
enum class GlyphSet : int { Auto = 0, PlayStationClassic, PlayStation, Xbox, Nintendo, Steam };
inline constexpr int kGlyphSets = 6;

enum class GlyphDirection : int { Up = 0, Right, Down, Left };

// A value outside the list is Auto.
GlyphSet glyph_set_from(int value);
// Name of a set on the Controls screen.
const char* glyph_set_label(GlyphSet set);

// The first connected controller, as SDL describes it.
struct PadIdentity {
    int type = -1;  // SDL_GamepadType, -1 with no controller
    int vendor = 0;
    int product = 0;
    bool on_steam_deck = false;  // the game runs on a Steam Deck
};
// The lettering Auto means for a controller.
GlyphSet glyph_set_for(const PadIdentity& pad);

// A key or mouse bind by its config name ("W", "Left Shift", "Keypad 5", "Mouse1"); None when the sheet has no
// picture for it.
Glyph glyph_for_key(std::string_view bind_name);
// A controller bind by its button name ("a", "leftshoulder", "dpup"); the empty button for anything else.
Glyph glyph_for_pad(GlyphSet set, std::string_view button_name);
// One direction of the left stick, or of the d-pad.
Glyph glyph_for_stick(GlyphSet set, GlyphDirection direction, bool dpad);

// Where a prompt is on the sheet, in the texture page's own texels (the sheet stands at twice that size), and how
// wide it is drawn on the 320x240 screen.
struct GlyphCell {
    int u = 0;
    int v = 0;
    int width = 0;
    int screen_width = 0;
};
GlyphCell glyph_cell(Glyph glyph, bool dark);

// Height of a prompt on the 320x240 screen.
inline constexpr int kGlyphScreenSize = 16;

}  // namespace sh::input
