// SPDX-License-Identifier: GPL-3.0-or-later
// The arithmetic of the screens shown before the game (the quick setup and the disc import): where lines of text
// break, how large a canvas unit is on a window of any size and density, how far a list scrolls and where a row of
// buttons goes. No SDL here, so the tests check it directly.
#pragma once

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::screen_kit {

// The pieces a line may break between: a word with the spaces after it, or one character of the scripts written
// without spaces (CJK). A '\n' is a piece of its own and forces a break.
std::vector<std::string> break_pieces(std::string_view text);

// Width of a line in the caller's units.
using Measure = std::function<float(std::string_view line)>;

// `text` in lines no wider than `max_width`. A word wider than a line breaks after a path separator, else between
// characters. A closing mark or full stop of CJK text never starts a line. Spaces at the ends of lines are dropped.
std::vector<std::string> wrap_lines(std::string_view text, float max_width, const Measure& measure);

// The part of a window the screen may use: the window minus the system's insets (a notch, rounded corners).
struct Insets {
    float left = 0, top = 0, right = 0, bottom = 0;
};

// How canvas units map to pixels: x_px = origin_x + x * scale. `width` and `height` are the usable area in canvas
// units; the area starts at canvas (0, 0).
struct View {
    float scale = 1.0f;
    float origin_x = 0.0f;
    float origin_y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

// The canvas size a layout is written for.
struct Size {
    float width = 0, height = 0;
};

// A canvas that grows with the window: the scale fits the design that suits the area inside the insets best (a wide
// one and a tall one, say), but never drops under `min_scale` (pixels per unit: the display's density, so a
// finger-sized button stays finger sized) nor rises over `max_scale`. The canvas then takes the whole area, wider or
// taller than the design.
View fit_view(int pixel_w, int pixel_h, Insets insets, std::span<const Size> designs, float min_scale, float max_scale);

// A scroll offset kept inside [0, content - viewport] (0 when everything fits).
float clamp_scroll(float offset, float content, float viewport);

// One button of a row laid out by lay_out_buttons, in the units of `left` and `right`.
struct ButtonSlot {
    float x0 = 0, x1 = 0;
    int row = 0;
};

// Buttons of the given text widths between `left` and `right`: one centred row of equal buttons (each as wide as
// the widest, at least `min_width`) when they fit, otherwise one full-width button per row, in order.
std::vector<ButtonSlot> lay_out_buttons(std::span<const float> widths, float left, float right, float gap,
                                        float min_width);

}  // namespace sh::screen_kit
