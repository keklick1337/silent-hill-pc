// SPDX-License-Identifier: GPL-3.0-or-later
//
// The touch screen as an input device (docs/BUILDING.md section 5, README "Touch controls").
//
// In free gameplay the on-screen controls show: a floating stick in the left part of the screen (the pad's analog
// stick, the rim runs), the action button while something in reach answers it, aim, attack while the weapon is up
// (or while the game asks for it: a kick at a downed enemy), the camera (View), the map, the inventory and pause.
// With `touch_movement = dpad` a fixed cross of four arrows (one finger, a diagonal presses two; the pad's digital
// directions, as on the original's d-pad) and a run toggle stand in for the stick.
// While Harry is held the game asks for the struggle button: its taps and the stick's flicks are presses to break
// free (engine/input/touch_prompts.h). They hold the same input actions a keyboard does (pc_actions.h), so the game
// sees pad buttons only. Everywhere else a finger is a pointer: menus follow it as they follow the mouse, a drag
// scrolls a list (and glides on), a tap selects; screens that do not follow the mouse (messages, a scene) take a tap
// as Cross. A Back button is shown where Back does something, a Skip button where the game lets the player skip.
// The paper map is a page under the fingers: a pinch zooms about them, a drag pans and glides on, a double tap
// zooms in and out, and at the whole page a vertical swipe turns it. The layout editor (Options > Controls > Touch Controls)
// moves and sizes the controls (config.cfg `touch_layout`).
#pragma once

namespace sh::input::touch {

// Once the game window is open: takes the fingers, the pad system's touch stick and its skip.
void install();

// The on-screen controls are in use: a touch device with `touch_controls` not off, or `touch_controls = on`.
bool controls_enabled();

// Called from the debug overlay's draw: the controls over the frame.
void draw();
// Called while a movie holds the loop, over its picture: the Skip button.
void draw_over_movie();

// The layout editor, opened from the options.
void open_editor();
bool editor_open();
// Back to the default places (and writes config.cfg).
void reset_layout();

// A finger is down as the pointer and has not started a drag: a menu shows the row under it pressed.
bool pointer_pressed();

// A list that scrolls under the finger by itself (the menu view) claims the pointer's drags on every frame it is
// drawn: the pointer then follows a dragging finger instead of turning the wheel, a finger that rests does not
// become a long press, and while the list still glides fast (`flinging`) a finger that lands only stops it.
void claim_list_drag(bool flinging);

// The pointer's finger as such a list needs it.
struct ListFinger {
    bool down = false;        // a finger is the pointer
    bool dragging = false;    // it moved past the tap distance: it scrolls, its lift picks nothing
    bool horizontal = false;  // the drag goes sideways (a bar under it is set)
    bool stop_only = false;   // it landed on a gliding list
};
ListFinger list_finger();

}  // namespace sh::input::touch

// The C name pc_quick_actions.cpp asks: auto reload while played by touch (a touch screen has no reload button).
extern "C" int Pc_Touch_Active(void);
