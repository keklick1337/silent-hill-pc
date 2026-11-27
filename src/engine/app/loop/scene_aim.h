// SPDX-License-Identifier: GPL-3.0-or-later
//
// A frame's scene seen from another camera than the one the game drew it with: the frame loop presents the scene
// from the alternate cameras' unrounded view, and between two updates from where the mouse has turned it since
// (input/alt_camera_view.h). Nothing here feeds back into the game.
#pragma once

#include <array>

#include "render/scene/frame_blend.h"
#include "render/scene/scene.h"

namespace sh::loop {

// The level camera (no roll) at `eye` looking at `at`, both in Q12 world units. False when it looks straight up or
// down or nowhere.
bool camera_looking(const double eye[3], const double at[3], sh::scene::Camera* camera);

// How far a camera the game drew with is from `exact`, a camera of camera_looking(): the largest difference of
// their rotation's elements, and how far from its own eye `drawn` sees the eye of `exact`, in scene units (256 a
// metre). The game's camera is rounded (its rotation to 1/4096), so its eye is asked of the camera as it draws, not
// taken from its matrix as if the matrix were a pure rotation: far from the world's origin that is centimetres off.
struct CameraGap {
    double turn = 0.0;
    double move = 0.0;
};
CameraGap camera_gap(const sh::scene::Camera& drawn, const sh::scene::Camera& exact);

// Where a camera stands in the world (scene units): the point it sees at its own eye, also for a rounded camera.
std::array<double, 3> camera_eye(const sh::scene::Camera& camera);

// The frame and its flashlight beam as the camera `to` sees them: what they hold in view space (the posed meshes,
// their patch normals, the flakes, the beam) stays where it is in the world. The world is where the frame's own
// camera, rounded as it is, drew it from.
void aim_scene(sh::scene::Frame& frame, sh::scene::Beam& beam, const sh::scene::Camera& to);

}  // namespace sh::loop
