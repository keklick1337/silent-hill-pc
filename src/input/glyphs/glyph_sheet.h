// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The input prompt sheet in the running game: the picture (assets/icons/input_glyphs.png) is handed to the
// texture overrides once, under a texture page and a palette key that no texture of the disc uses, and the 2D
// drawing addresses it by those like any page; and the lettering that is in effect (the gamepad_icons setting, or
// the connected controller's when the setting is Auto).

#pragma once

#include "input/glyphs/glyph_table.h"

namespace sh::input {

// The page the sheet is addressed by, and the palette cell its override is keyed on: the last palette position of
// video memory, which no texture of the game looks up.
inline constexpr int kGlyphSheetPageX = 448;
inline constexpr int kGlyphSheetKeyX = 1008;
inline constexpr int kGlyphSheetKeyY = 511;

class GlyphSheet {
public:
    // Reads and registers the picture the first time; later calls do nothing.
    void load();
    // False when the picture could not be used: prompts are then drawn as text.
    bool ready() const { return ready_; }
    // Never Auto.
    GlyphSet active_set();
    // The gamepad_icons setting.
    GlyphSet chosen_set() const;
    // Moves the setting to the next or the previous lettering and saves it.
    void step_chosen_set(int direction);

private:
    bool tried_ = false;
    bool ready_ = false;
    int logged_auto_ = -1;
};

GlyphSheet& glyph_sheet();

}  // namespace sh::input
