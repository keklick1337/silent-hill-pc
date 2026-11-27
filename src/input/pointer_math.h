// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The parts of the mouse pointer (mouse_pointer.h) that are arithmetic over samples: button edges, wheel steps,
// where a point of the picture is in the menus' coordinates, who moves a puzzle's cursor, and the arrow's pixels.
// No SDL, no renderer, no game.

#pragma once

#include <cstdint>
#include <span>

namespace sh::input {

// A button sampled once a frame.
struct ButtonEdge {
    bool down = false;
    bool pressed = false;  // went down on the last sample

    void sample(bool now) {
        pressed = now && !was_down_;
        down = now;
        was_down_ = now;
    }
    // The pointer is off: nothing is pressed, and a button held through that is a press when it comes back.
    void release() {
        pressed = false;
        was_down_ = false;
    }

private:
    bool was_down_ = false;
};

// The bit of a mouse button in the device's button state (bit 0 left, 1 middle, 2 right, 3 and 4 the side
// buttons), from the number a bind gives it: Mouse1 left, Mouse2 right, Mouse3 middle, Mouse4 and Mouse5 the side
// buttons. 0 for a number that is no button.
constexpr std::uint32_t mouse_button_bit(int bind_number) {
    constexpr std::uint32_t kBits[] = {0, 1u << 0, 1u << 2, 1u << 1, 1u << 3, 1u << 4};
    return bind_number >= 1 && bind_number <= 5 ? kBits[bind_number] : 0;
}

// The wheel as the menus read it. The input layer latches every notch for two frames and counts all of them.
struct WheelReader {
    int step = 0;     // +1 / -1 on the frame a notch arrives (several in one frame are one step)
    int notches = 0;  // every notch since the last sample, up positive

    void sample(int up_frames, int down_frames, int total);
    // The pointer is off: what is spun meanwhile must not arrive in one burst afterwards.
    void skip(int total);

private:
    int last_up_ = 0;
    int last_down_ = 0;
    int last_total_ = 0;
};

// A point of the picture (0..1 across and down) in the coordinates menus lay their text out in: 320 wide with
// x = 160 in the middle, and vertically the frame's own height centred on y = 112. `wide` is how much wider than
// 320 the 2D space currently is on screen (1 when it is not).
struct MenuPoint {
    float x = 0.f;
    float y = 0.f;
};
inline constexpr int kMenuCentreX = 160;
inline constexpr int kMenuCentreY = 112;
MenuPoint menu_point(float frac_x, float frac_y, float wide, int frame_width, int frame_height);

// Whole screen pixels per pixel of the 32-pixel arrow, from the picture's height.
int pointer_scale(int picture_height);

// A puzzle's free cursor follows whichever of the stick, the mouse and a controller's touchpad moved last, so a
// resting mouse never holds the cursor against a stick.
enum class CursorOwner { Stick, Mouse, Touchpad };
CursorOwner next_cursor_owner(CursorOwner now, int stick_x, int stick_y, int stick_threshold, bool mouse_moved,
                              bool touch_moved);

// One finger on a controller's touchpad, used as a laptop's: only its movement counts.
class TouchDrag {
public:
    // `finger` is 0 for none, else an id of the controller it is on; x and y are 0..1 across the pad. The
    // movement comes out in frame pixels: a full swipe across the pad is the frame's width.
    void sample(std::uint32_t finger, float x, float y, int frame_width);
    // Nobody reads the pad now: the next touch starts a new drag.
    void lift() { dragging_ = false; }

    float dx = 0.f;
    float dy = 0.f;
    // The controller whose finger was followed last; with several pads touched it keeps the cursor.
    std::uint32_t last_finger() const { return finger_; }

private:
    bool dragging_ = false;
    std::uint32_t finger_ = 0;
    float x_ = 0.f;
    float y_ = 0.f;
};

// A 16-colour picture of 4-bit pixels, four to a word with the leftmost in the low bits, through a 15-bit palette
// (5 bits a channel, red lowest; the value 0 is see-through) into 8-bit RGBA. False, and nothing written, when the
// palette is all zero - the picture is not there yet.
bool decode_indexed_picture(std::span<const std::uint16_t> words, std::span<const std::uint16_t, 16> palette,
                            int width, int height, std::uint8_t* rgba);

}  // namespace sh::input
