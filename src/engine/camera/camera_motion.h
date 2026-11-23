#pragma once

// Generic fixed-point motion helpers of the camera: a speed that accelerates toward a target and
// brakes near it, the planar version with a side damping, and a piecewise-linear table lookup.
// All Q12; the results are the shipped game's to the last bit.

#include <cstdint>

#include "engine/camera/camera_types.h"

namespace sh::camera {

// Linear interpolation in a table of `count` values spread evenly over [min_x, max_x]; outside the
// range the end values. The interval width is the integer (max_x - min_x) / (count - 1).
std::int32_t piecewise_linear(const std::int32_t* values, std::int32_t count, std::int32_t x, std::int32_t min_x,
                              std::int32_t max_x);

// One axis: the speed accelerates toward `target` from `position` by accel * dt, is capped at
// +-max_speed, and is limited to brake_per_distance * |target - position| so it arrives without
// overshoot.
std::int32_t speed_toward(std::int32_t speed, std::int32_t position, std::int32_t target, std::int32_t accel,
                          std::int32_t max_speed, std::int32_t brake_per_distance, std::int32_t dt);

// The angular version: the angle difference is wrapped to a half turn first.
std::int32_t angular_speed_toward(std::int32_t speed, std::int32_t angle, std::int32_t target_angle,
                                  std::int32_t accel, std::int32_t max_speed, std::int32_t brake_per_distance,
                                  std::int32_t dt);

// XZ velocity steering toward `target`: accelerate along the direction to it, cap the total speed,
// limit the speed along the direction by the remaining distance (beyond `arrive_radius`), and damp
// the sideways part by side_brake * dt.
void steer_planar_velocity(std::int32_t& vx, std::int32_t& vz, const Vec3& position, const Vec3& target,
                           std::int32_t arrive_radius, std::int32_t accel, std::int32_t max_speed,
                           std::int32_t brake_per_distance, std::int32_t side_brake, std::int32_t dt);

// The change of a value limited to `rate_max` per second, as a rate (Q.12 per second): `to - from` is clamped
// to +-rate_max * dt (64-bit product) and divided by dt. Zero when dt is zero.
std::int32_t clamped_rate_toward(std::int32_t from, std::int32_t to, std::int32_t rate_max, std::int32_t dt);

// Direction vector of radius `r` from a pitch (angle_x) and a yaw (angle_y), all Q.12; the components wrap to 16 bits
// like the game's short vectors.
void direction_from_angles(std::int16_t angle_x, std::int16_t angle_y, std::int32_t r, std::int16_t out[3]);

// The pitch and yaw of a short direction vector; returns its length (the game's square root of the sum of squares).
std::int32_t angles_from_direction(const std::int16_t vec[3], std::int16_t& angle_x, std::int16_t& angle_y);

}  // namespace sh::camera
