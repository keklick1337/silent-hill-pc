// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#pragma once

// The exact twins of the game's matrices (docs/RENDER.md, sections 5.3 and 7.5). The game keeps rotations
// in Q12 integers and translations in whole units; with precise vertices on, whoever builds a matrix also notes the
// values it had before rounding, and a projection that loads the matrix gets those back. A twin is trusted only
// while the integers it was noted for are still the integers of the matrix, and only until the frame ends.
//
// Twins are found three ways:
//   - by the address of a matrix record (note_*, *_of, copy, forget_*);
//   - by the integers themselves, for matrices built this frame from the camera (register_*, derived_*);
//   - for the view loaded into the geometry state (*_loaded, current_*).
//
// Everything here does nothing and finds nothing while precise vertices are off (sh::render::precise().enabled).

#include "engine/math/fixed_geometry.h"

namespace sh::math::capture {

// ---- Twins of matrix records, by address ----------------------------------------------------------------------
// `record` is a Matrix3 or a Transform as the game holds it; rotations are at unit scale (integer / 4096).

void note_rotation(const void* record, const double exact[9]);
void note_translation(const void* record, const double exact[3]);
// The record's integers changed and nobody knows the exact values: an older twin at this address is not to be used.
void forget_rotation(const void* record);
void forget_translation(const void* record);
// `to` now holds what was computed in `from`: it takes over the rotation twin of `from`.
void copy_rotation(const void* to, const void* from);
// The twin of a record. A record without one gets the best that can be said of its integers (the camera's or a
// derived matrix's twin, else the integers themselves), so these fail only while precise vertices are off.
bool rotation_of(const void* record, double out[9]);
bool translation_of(const void* record, double out[3]);

// ---- The view loaded into the geometry state --------------------------------------------------------------------

// The geometry state just loaded its rotation (translation) from `record`: the twin of that record becomes the
// twin of the loaded view. A null record, or one whose integers are not the loaded ones, leaves the view without.
void rotation_loaded(const void* record);
void translation_loaded(const void* record);
// The twin of the loaded view; false when none stands for the integers loaded now.
bool current_rotation(double out[9]);
bool current_translation(double out[3]);

// ---- The camera of the frame --------------------------------------------------------------------------------------

// The unquantised camera of the frame (smooth_models with precise vertices): the world-to-view rotation at unit
// scale, the world-screen translation and the eye in geometry units, standing for the integer world-screen matrix
// `key`. Every draw path that starts from the camera reads it, so models, the map, effects and decals move together
// and none of them steps with the integer camera.
struct CameraTwin {
    Transform key;
    double rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    double trans[3] = {0, 0, 0};
    double eye[3] = {0, 0, 0};
};
void set_camera(const CameraTwin& twin);
void clear_camera();
// The twin when `rot`/`trans` are the camera's integer world-screen matrix, else null.
const CameraTwin* camera_of(const Matrix3& rot, const Vec3i& trans);
// The twin in force, or null.
const CameraTwin* camera();

// Matrices built this frame from the camera, found again by their integers when a drawer projects from a record
// that has no twin at its address. Noted only while a camera twin is in force; either part may be null.
void register_derived(const Transform& integer, const double rot[9], const double trans[3]);
// The camera moved to `point` (geometry units; the integer result `integer`, from the world-screen matrix `ws`).
void register_camera_point(const Transform& ws, const double point[3], const Transform& integer);
// The camera's rotation turned by `turn` (the integer product in `integer`, from the world-screen matrix `ws`).
void register_camera_turn(const Transform& ws, const Matrix3& turn, const Transform& integer);
// Unquantised rotation (unit scale) or translation of a matrix with these integers: the camera's, a derived one, or
// false.
bool derived_rotation(const Matrix3& rot, double out[9]);
bool derived_translation(const Vec3i& trans, double out[3]);

// ---- The frame -----------------------------------------------------------------------------------------------------

// Once per presented update: every twin noted so far retires (the camera twin stays until it is set again).
void end_frame();

}  // namespace sh::math::capture
