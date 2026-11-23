// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "input/pointer_math.h"

#include <algorithm>
#include <cmath>

namespace sh::input {
namespace {

// A notch's latch starts at this many frames.
constexpr int kLatchStart = 2;

// SDL gives both axes of a touchpad as 0..1, and the pads are about twice as wide as they are tall.
constexpr float kTouchpadHeightToWidth = 0.5f;

}  // namespace

void WheelReader::sample(int up_frames, int down_frames, int total) {
    notches = total - last_total_;
    last_total_ = total;
    step = 0;
    if (up_frames == kLatchStart && last_up_ < kLatchStart) {
        step = 1;
    } else if (down_frames == kLatchStart && last_down_ < kLatchStart) {
        step = -1;
    }
    last_up_ = up_frames;
    last_down_ = down_frames;
}

void WheelReader::skip(int total) {
    step = 0;
    notches = 0;
    last_total_ = total;
}

MenuPoint menu_point(float frac_x, float frac_y, float wide, int frame_width, int frame_height) {
    MenuPoint p;
    p.x = ((frac_x - 0.5f) * wide + 0.5f) * static_cast<float>(frame_width);
    p.y = frac_y * static_cast<float>(frame_height) - static_cast<float>(frame_height / 2) +
          static_cast<float>(kMenuCentreY);
    return p;
}

int pointer_scale(int picture_height) {
    return std::max(1, static_cast<int>(std::floor(static_cast<double>(picture_height) / 960.0 + 0.5)));
}

CursorOwner next_cursor_owner(CursorOwner now, int stick_x, int stick_y, int threshold, bool mouse_moved,
                              bool touch_moved) {
    if (std::abs(stick_x) >= threshold || std::abs(stick_y) >= threshold) {
        return CursorOwner::Stick;
    }
    if (mouse_moved) {
        return CursorOwner::Mouse;
    }
    return touch_moved ? CursorOwner::Touchpad : now;
}

void TouchDrag::sample(std::uint32_t finger, float x, float y, int frame_width) {
    dx = dy = 0.f;
    // The first frame of a touch, or a touch that moved to another controller, only sets the starting point.
    if (finger != 0 && dragging_ && finger == finger_) {
        dx = (x - x_) * static_cast<float>(frame_width);
        dy = (y - y_) * static_cast<float>(frame_width) * kTouchpadHeightToWidth;
    }
    dragging_ = finger != 0;
    finger_ = finger;
    x_ = x;
    y_ = y;
}

bool decode_indexed_picture(std::span<const std::uint16_t> words, std::span<const std::uint16_t, 16> palette,
                            int width, int height, std::uint8_t* rgba) {
    if (std::all_of(palette.begin(), palette.end(), [](std::uint16_t c) { return c == 0; })) {
        return false;
    }
    const int words_per_row = width / 4;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::uint16_t word = words[static_cast<std::size_t>(y * words_per_row + x / 4)];
            const std::uint16_t colour = palette[(word >> (4 * (x % 4))) & 0xF];
            std::uint8_t* out = rgba + (static_cast<std::size_t>(y) * width + x) * 4;
            out[0] = static_cast<std::uint8_t>((colour & 0x1F) << 3);
            out[1] = static_cast<std::uint8_t>(((colour >> 5) & 0x1F) << 3);
            out[2] = static_cast<std::uint8_t>(((colour >> 10) & 0x1F) << 3);
            out[3] = colour == 0 ? 0 : 255;
        }
    }
    return true;
}

}  // namespace sh::input
