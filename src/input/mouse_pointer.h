// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The mouse as a pointer: menus that can be clicked, and the free cursor of the puzzles that have one (piano,
// plates, lift and door panels) placed under the mouse or pushed by a controller's touchpad. A convenience the
// original does not have, on while `mouse_cursor` is on and the system pointer is free - so in menus, puzzles and
// with the original camera, and not while an alternate camera has captured the mouse to look around
// (camera_style.h) or the console is open.

#pragma once

#include "input/pointer_math.h"

#include <cstdint>

namespace sh::input {

class MousePointer {
public:
    // Once a frame, after the pad state of the frame is built and after the camera style's update: samples the
    // mouse, and while a puzzle cursor is on screen turns clicks into the game's confirm and cancel.
    void frame_update();

    // --- puzzles ---
    // A puzzle calls this between reading the stick and limiting its cursor: the position (12 fractional bits,
    // frame pixels from the centre) becomes the point under the mouse, or moves by the touchpad drag, depending on
    // who moved last. Nothing happens while the stick has the cursor.
    void puzzle_override(int* cursor_x, int* cursor_y);
    // The puzzle drew its cursor at this frame position. Keeps puzzle mode alive for a few frames, and when the
    // puzzle's own limits stopped the cursor short of the mouse, brings the system pointer back onto it.
    void puzzle_cursor_drawn(int x, int y);
    bool puzzle_active() const { return puzzle_frames_ > 0; }

    // --- menus ---
    // The pointer in menu text coordinates / as a 0..1 fraction of the picture. False (and nothing written) while
    // the pointer is off or outside the picture. A null output is skipped.
    bool menu_position(int* x, int* y) const;
    bool picture_position(float* x, float* y) const;
    // True on a frame the mouse moved: a resting mouse must not fight keys or the pad over a selection.
    bool moved() const { return moved_; }
    bool left_clicked() const;
    bool left_held() const;
    bool right_clicked() const;
    // +1 / -1 on a new notch; every notch of the frame, up positive (a fast spin in a long list needs them all).
    int wheel_step() const;
    int wheel_notches() const;
    // Pad buttons the mouse holds right now through binds (interact = Mouse1 and the like), with the d-pad's
    // directions repeated as the left stick's. A screen that reads clicks itself takes these out of the pad
    // buttons it reads, or one click acts twice.
    std::uint32_t bound_pad_bits() const;

    // --- the picture of the pointer ---
    // A screen that wants the pointer drawn asks every frame it should show.
    void show();
    // A screen asked lately, or a puzzle's cursor is up: the screen follows the pointer (the touch screen's taps).
    bool requested() const { return show_presents_ > 0 || puzzle_active(); }
    // Last thing of the frame's 2D: draws the arrow if a screen asked for it.
    void draw();

private:
    bool enabled() const;
    void poll_touchpad();
    void pull_pointer_to(float tip_x, float tip_y);

    MenuPoint at_;              // menu text coordinates
    float picture_x_ = 0.f;     // 0..1 inside the picture
    float picture_y_ = 0.f;
    bool in_picture_ = false;
    bool moved_ = false;
    int window_x_ = -1;
    int window_y_ = -1;
    ButtonEdge left_;
    ButtonEdge right_;
    WheelReader wheel_;

    int puzzle_frames_ = 0;
    CursorOwner owner_ = CursorOwner::Stick;
    // Where the override put the cursor this frame, to compare with where the puzzle then drew it.
    float target_x_ = 0.f;
    float target_y_ = 0.f;
    bool overrode_ = false;
    TouchDrag touch_;
    bool touch_button_ = false;
    bool touch_clicked_ = false;

    int show_presents_ = 0;
    std::uint32_t arrow_texture_ = 0;
};

MousePointer& mouse_pointer();

}  // namespace sh::input
