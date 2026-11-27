// SPDX-License-Identifier: GPL-3.0-or-later
//
// The alternate cameras' view for the picture (alt_camera_host.cpp). The game turns its camera once an update and
// in whole angle units; the picture is drawn from the same camera without the rounding, and on a frame presented
// between two updates along the angles the mouse and the right stick have reached by then. So the view turns on
// every presented frame by exactly what the hand did, at any frame rate. Nothing here feeds back into the game:
// the mouse counts read between two updates are handed to the next update, which turns the game's camera by them.

#pragma once

namespace sh::input {

// Q12 world units.
struct AltCameraView {
    double eye[3] = {};
    double at[3] = {};
};

// An update begins: the alternate camera has not made its view yet.
void alt_view_update_begin();
// The update's scene is about to be presented. `follows`: the frames up to the next update show the way from the
// previous presented update to this one. False when the update's view was not an alternate camera's; otherwise
// `told` is the view the game's own rounded camera was given, for the caller to see that the scene was drawn
// with it.
bool alt_view_update_presented(bool follows, AltCameraView* told);

struct AltViewMoment {
    // The mouse is read for the frame: its counts since the last update turn the view, and go to the next update.
    bool read_mouse = false;
    // The scene shown lies on the way to the update before the last one, not to the last one.
    bool before_last = false;
    // How far it is on the way from the update before that one to it (1: that update as it is).
    double blend = 1.0;
    // Seconds of game time since the last update, for the turn of a held stick.
    double seconds = 0.0;
};

// The view of the frame about to be presented. Only after alt_view_update_presented() said that the last update's
// view is an alternate camera's.
void alt_view_for_present(const AltViewMoment& moment, AltCameraView* view);

}  // namespace sh::input
