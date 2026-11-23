// SPDX-License-Identifier: GPL-3.0-or-later
//
// The draw layer of the menus the engine adds to the game (the Options tree, the pause menu, the save list, the
// controls screens): text through the game's text drawer, the selection widgets of the original options screen,
// rules, bars and backgrounds, and one frame of menu input.
//
// Every coordinate is in one 320 x 240 space (x 0..320, y 0..224) on both of the game's screens, so a layout
// works unchanged on the 448-line title screen and the 224-line screen in play. A row of text at y takes y..y+16.
// In a right-to-left language the layer mirrors itself about the screen's middle (nm_draw.cpp).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "engine/menu/text_fit.h"
#include "engine/menu/yes_no_prompt.h"

namespace sh::ui::nm {

// A label's x from the x of the bullet in front of it, as on the original Options pages.
inline constexpr int kBulletTextDx = 24;

// One frame of what the player did, as the menus read it.
struct Input {
    // Held directions repeat.
    bool up = false, down = false, left = false, right = false;
    bool mirrored = false;  // left/right are mirrored with the layout (a right-to-left language)
    bool confirm = false, cancel = false;
    int wheel = 0;  // notches, positive towards the top
    // The pointer, in the layer's space; `pointer` is false while the mouse cursor is not in use.
    bool pointer = false;
    int pointer_x = 0, pointer_y = 0;
    bool pointer_moved = false, click = false, right_click = false;
};
Input read_input();

// `s` goes through the menu localization; text_verbatim draws it as it is (measure that with
// sh::i18n::text_width).
void text(int x, int y, int color, const char* s);
void text_verbatim(int x, int y, int color, const char* s);
void text_centred(int cx, int y, int color, const char* s);
void text_right(int right_x, int y, int color, const char* s);
int text_width(const char* s);
// Draws `s` as it is in x..x+max_w. Text wider than that pans to its end and starts over, in step with any other
// pan on screen, when `pan` is set (the selected row, or text that cannot be selected), and is cut with "..."
// otherwise. Every pan restarts from its left end when the highlight moves. Widths are at the text drawer's
// current size. Returns the width drawn, at most max_w.
int text_fitted(int x, int y, int color, const char* s, int max_w, bool pan);
// Data text (a file name, a key's name, a name out of a mod) spelled for the game font, which draws UTF-8 and takes
// '_' for a space (sh::menu::font_spelling over the game font and its fallbacks).
std::string font_text(std::string_view utf8, std::size_t max_bytes = sh::menu::kFitLineBytes);
// While on, text drawn at another size than the font's own is not filtered as world geometry. Only for menus
// drawn over a frozen capture of the world; the caller turns it off again.
void keep_text_unfiltered(bool on);

// The gold underline of the selected row: y_bottom is the row's y + 14, x0 the bullet's x - 1. It glides to a
// new place; snap_highlight makes the next one jump instead (a row picked with the pointer).
void highlight(int x0, int x1, int y_bottom);
void snap_highlight();
// x is the bullet's left edge, y the y of the label beside it.
void bullet(int x, int y, bool selected);
// The arrows either side of a value whose text spans x_left..x_right on the row at y.
void value_arrows(int x_left, int x_right, int y);
// An arrow above or below a list that scrolls; y is its vertical centre.
void scroll_arrow(int cx, int y, bool up, bool enabled);
void rule(int x0, int x1, int y);
// The options screen's volume bar: `notches` notches from x, the first `fill` (0..4096 of the whole bar) lit; y is
// the y of the label beside it.
void bar(int x, int y, int notches, int fill);

// Under a menu: black, or a dark veil over the game.
void black_background();
void dim_background();

// A point of the layer in the units and origin the frame's 2D commands are drawn in; `y_scale` is how many of
// those units a line of the layer is tall (2 on the 448-line screen).
struct FramePoint {
    float x = 0.0f, y = 0.0f, y_scale = 1.0f;
};
FramePoint to_frame(int x, int y);
// Posed mesh `item` of the scene frame is drawn after the menu's backing and before everything else of the menu.
void link_scene_item(int item);

// The question and its two answers, centred, the question at y `cy`. Draws nothing while the prompt is closed.
void yes_no(const sh::menu::YesNoPrompt& prompt, const char* question, int cy);

}  // namespace sh::ui::nm
