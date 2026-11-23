#include "engine/camera/camera_motion.h"

#include "engine/camera/camera_math.h"
#include "engine/world/fixed_math.h"

namespace sh::camera {

namespace {

// Removes the part of (vx, vz) along `heading` that exceeds `limit`.
void cap_along(std::int32_t& vx, std::int32_t& vz, std::int32_t limit, std::int16_t heading) {
    const std::int32_t dir_x = fx::sin(heading);
    const std::int32_t dir_z = fx::cos(heading);
    const std::int32_t excess = (fx::mul_wide(vx, dir_x) + fx::mul_wide(vz, dir_z)) - limit;
    if (excess > 0) {
        vx -= fx::mul_wide(excess, dir_x);
        vz -= fx::mul_wide(excess, dir_z);
    }
}

// Damps the part of (vx, vz) across `heading`: it is clamped to +-max_side, then moved toward zero by
// `brake` (reaching zero when within the brake).
void damp_across(std::int32_t& vx, std::int32_t& vz, std::int32_t brake, std::int32_t max_side, std::int16_t heading) {
    const std::int32_t across_dir = heading + fx::angle(90.0f);
    const std::int32_t original = fx::mul_wide(vx, fx::sin(across_dir)) + fx::mul_wide(vz, fx::cos(across_dir));
    std::int32_t side = fx::clamp(original, -max_side, max_side);
    if (brake < side) {
        side = side - brake;
    } else if (side >= -brake) {
        side = 0;
    } else {
        side = side + brake;
    }
    vx += fx::mul_wide(side - original, fx::sin(across_dir));
    vz += fx::mul_wide(side - original, fx::cos(across_dir));
}

}  // namespace

std::int32_t piecewise_linear(const std::int32_t* values, std::int32_t count, std::int32_t x, std::int32_t min_x,
                              std::int32_t max_x) {
    if (x >= max_x) {
        return values[count - 1];
    }
    if (x < min_x) {
        return values[0];
    }
    const std::int32_t width = (max_x - min_x) / (count - 1);
    const std::int32_t into = (x - min_x) % width;
    const std::int32_t interval = (x - min_x) / width;
    if (interval >= count - 1) {
        return values[count - 1];
    }
    if (interval < 0) {
        return values[0];
    }
    return (fx::wrap_mul(values[interval], width - into) + fx::wrap_mul(values[interval + 1], into)) / width;
}

std::int32_t speed_toward(std::int32_t speed, std::int32_t position, std::int32_t target, std::int32_t accel,
                          std::int32_t max_speed, std::int32_t brake_per_distance, std::int32_t dt) {
    const std::int32_t gap = target - position;
    std::int32_t next = gap >= 0 ? fx::mul_wide(accel, dt) + speed : speed - fx::mul_wide(accel, dt);
    next = fx::clamp(next, -max_speed, max_speed);
    const std::int32_t arrive_limit = fx::mul_wide(brake_per_distance, gap < 0 ? -gap : gap);
    if (gap >= 0) {
        return arrive_limit >= next ? next : arrive_limit;
    }
    return -arrive_limit < next ? next : -arrive_limit;
}

std::int32_t angular_speed_toward(std::int32_t speed, std::int32_t angle, std::int32_t target_angle,
                                  std::int32_t accel, std::int32_t max_speed, std::int32_t brake_per_distance,
                                  std::int32_t dt) {
    return speed_toward(speed, 0, fx::wrap_angle(target_angle - angle), accel, max_speed, brake_per_distance, dt);
}

void steer_planar_velocity(std::int32_t& vx, std::int32_t& vz, const Vec3& position, const Vec3& target,
                           std::int32_t arrive_radius, std::int32_t accel, std::int32_t max_speed,
                           std::int32_t brake_per_distance, std::int32_t side_brake, std::int32_t dt) {
    const std::int16_t toward = static_cast<std::int16_t>(fx::atan2(target.x - position.x, target.z - position.z));

    const std::int32_t gain = fx::mul_wide(accel, dt);
    vx += fx::mul(gain, fx::sin(toward));
    vz += fx::mul(gain, fx::cos(toward));

    const std::int32_t speed = fx::length(vx, 0, vz);
    if (max_speed < speed) {
        const std::int32_t excess = speed - max_speed;
        const std::int32_t heading = fx::atan2(vx, vz);
        vx -= fx::mul_wide(excess, fx::sin(heading));
        vz -= fx::mul_wide(excess, fx::cos(heading));
    }

    const std::int32_t dx = target.x - position.x;
    const std::int32_t dz = target.z - position.z;
    const std::int32_t heading_to_target = fx::atan2(dx, dz);
    std::int32_t arrive_limit = fx::mul_wide(brake_per_distance, fx::length(dx, 0, dz) - arrive_radius);
    if (arrive_limit < 0) {
        arrive_limit = 0;
    }

    cap_along(vx, vz, arrive_limit, static_cast<std::int16_t>(heading_to_target));
    damp_across(vx, vz, fx::mul_wide(side_brake, dt), arrive_limit >> 1, static_cast<std::int16_t>(heading_to_target));
}

std::int32_t clamped_rate_toward(std::int32_t from, std::int32_t to, std::int32_t rate_max, std::int32_t dt) {
    const std::int32_t range = fx::mul_wide(rate_max, dt);
    const std::int32_t diff = fx::clamp(to - from, -range, range);
    if (dt == 0) {
        return 0;
    }
    // The game's Q12 macro on an integer is a plain 32-bit multiply by 4096 (no float).
    return fx::wrap_mul(diff, 4096) / dt;
}

void direction_from_angles(std::int16_t angle_x, std::int16_t angle_y, std::int32_t r, std::int16_t out[3]) {
    const std::int32_t flat = fx::mul(r, fx::cos(angle_x));
    out[1] = static_cast<std::int16_t>(fx::mul(-r, fx::sin(angle_x)));
    out[0] = static_cast<std::int16_t>(fx::mul(flat, fx::sin(angle_y)));
    out[2] = static_cast<std::int16_t>(fx::mul(flat, fx::cos(angle_y)));
}

std::int32_t angles_from_direction(const std::int16_t vec[3], std::int16_t& angle_x, std::int16_t& angle_y) {
    using world::game_sqrt;
    using world::wrap_add;
    using world::wrap_mul;
    const std::int32_t x2 = wrap_mul(vec[0], vec[0]);
    const std::int32_t y2 = wrap_mul(vec[1], vec[1]);
    const std::int32_t z2 = wrap_mul(vec[2], vec[2]);
    const std::int32_t length = game_sqrt(wrap_add(wrap_add(x2, y2), z2));
    angle_x = static_cast<std::int16_t>(fx::atan2(-vec[1], game_sqrt(wrap_add(x2, z2))));
    angle_y = static_cast<std::int16_t>(fx::atan2(vec[0], vec[2]));
    return length;
}

}  // namespace sh::camera
