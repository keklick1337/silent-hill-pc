#pragma once

// The fixed-point trigonometry of the world queries, as the game computes it: Q.12 angles where 4096 is a full
// turn, Q.12 results where 4096 is 1.0. Every function returns the game's value to the last bit:
//   - sine and cosine follow the game's runtime formula (a double-precision evaluation truncated to Q.12);
//   - atan2 and the vector length are the integer table algorithms of the original runtime, with the lookup
//     data in trig_table.inc.

#include <cstdint>

namespace sh::world {

// Sine of a Q.12 angle. Valid for any input; the angle wraps every 4096.
std::int32_t fixed_sin(std::int32_t angle_q12);

// Cosine of a Q.12 angle. Valid for any input; the angle wraps every 4096.
std::int32_t fixed_cos(std::int32_t angle_q12);

// Angle of the vector (x, y) as a Q.12 angle in (-2048, 2048]. (0, 0) gives 0.
std::int32_t fixed_atan2(std::int32_t y, std::int32_t x);

// Square root of a non-negative integer, the game's table algorithm (fixed_math.h game_sqrt); 0 for value <= 0.
std::int32_t fixed_sqrt(std::int32_t value);

// Length of a 3D Q.12 vector without overflow: large components are shifted down first (shift 4, 8 or 12 by
// the largest component's magnitude) and the root is shifted back, so the result is the game's approximation.
std::int32_t fixed_length3(std::int32_t x, std::int32_t y, std::int32_t z);

}  // namespace sh::world
