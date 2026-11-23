#pragma once

// Rotation matrices of the camera in the game's fixed-point form: 3x3 entries in Q12 stored in 16
// bits, translation in geometry units (Q8 of a metre). The products follow the integer rules of the
// console's geometry coprocessor that the game's camera results were made with: three 16 x 16
// products summed in 64 bits, shifted right by 12 and saturated to 16 bits.

#include <cstdint>

#include "engine/camera/camera_types.h"

namespace sh::camera {

// Rotation from Euler angles (Q12): R = Ry * Rx * Rz, each entry evaluated in double precision and
// truncated to Q12 exactly like the game's runtime does it.
Matrix rotation_yxz(const Angles& angles);

// a * b on the 3x3 part; the translation of the result is left zero.
Matrix multiply(const Matrix& a, const Matrix& b);

// The 3x3 transpose; the translation is kept.
Matrix transpose(const Matrix& m);

// m * v on the 3x3 part, each component saturated to 16 bits.
Angles apply(const Matrix& m, const Angles& v);

// Euler angles (x pitch, y yaw, z roll) of a rotation built by rotation_yxz.
Angles matrix_angles(const Matrix& m);

// Pitch and yaw of a direction vector (roll 0).
Angles vector_angles(const Angles& v);

}  // namespace sh::camera
