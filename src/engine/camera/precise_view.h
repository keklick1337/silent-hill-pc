#pragma once

// The unquantised camera for drawing (smooth_models with precise vertices, docs/RENDER.md section 7.5). The game's
// camera is integer: angles in 1/4096 turn, a Q12 rotation matrix and a translation in whole geometry units, so the
// whole picture steps when the camera moves slowly. This keeps a double camera next to it for the renderer only:
// the rig's position before the Q12-to-Q8 truncation and its rotation built in double, with the angle steps
// smoothed to stay within 3/4 of an angle unit of the integer camera. It is published as the precise-capture camera
// twin (engine/math/precise_capture.h), which every draw path starting from the camera reads. Logic and every
// integer the game reads are unchanged.

#include "engine/camera/camera_types.h"
#include "engine/camera/view_space.h"

namespace sh::camera {

// After the rig ran: `view_out` is the matrix the frame hands to the view system, `direct` whether it is the rig's
// own camera matrix (not a scripted look-at, the first-person bob or a head view, which keep the integer camera).
void update_precise_camera(const CameraState& state, const Matrix& view_out, bool direct);

// The view system made `ws` from the eye's world matrix: the camera twin for them, or none while smooth_models or
// precise vertices are off.
void publish_precise_view(const Matrix& eye_world, const WorldScreen& ws);

// The free camera (devtools, the editor) set `eye_matrix` from a look-at: it is drawn from its unrounded heading,
// pitch (angle units, positive looks down) and position (Q12 units) while that matrix is the eye. Cleared when the
// free camera ends.
void set_free_camera_view(const Matrix& eye_matrix, double yaw, double pitch, const double position[3]);
void clear_free_camera_view();

}  // namespace sh::camera
