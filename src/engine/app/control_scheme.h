// SPDX-License-Identifier: GPL-3.0-or-later
//
// The player's button assignments (config.cfg) into the input layer. The config keeps two sets: the original
// camera's, and one for the alternate cameras, whose buttons mean other things (mouse look, strafing).
#pragma once

namespace sh::app {

// The set of the camera in use: the alternate cameras' while one of them frames the picture, else the original's.
// At start, and whenever the camera style changes.
void apply_camera_control_scheme();

// The original camera's set whatever the camera: menus are always driven with it, so an alternate camera's
// assignments do not reach into them. apply_camera_control_scheme() puts the camera's own back on return to play.
void apply_classic_control_scheme();

}  // namespace sh::app
