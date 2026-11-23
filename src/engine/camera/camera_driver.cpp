// The camera driver's rules (camera_driver.h). Integer arithmetic as the game's: 32-bit products
// shifted by 12 (or divided by 4096 where the game divided), arithmetic shifts for unit changes.

#include "engine/camera/camera_driver.h"

#include "engine/camera/camera_math.h"
#include "engine/camera/camera_motion.h"
#include "engine/world/fixed_math.h"
#include "engine/math/fixed_geometry.h"

namespace sh::camera {

namespace {

// Q8 to Q12 and back, as the game's unit macros (shifts, so negative values round down).
constexpr std::int32_t q8_to_q12(std::int32_t v) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(v) << 4); }
constexpr std::int32_t q12_to_q8(std::int32_t v) { return v >> 4; }

// a * b / 4096 with the division truncating toward zero (the fly camera's rule, unlike the shift).
constexpr std::int32_t mul_div(std::int32_t a, std::int32_t b) { return fx::wrap_mul(a, b) / 4096; }

}  // namespace

Vec3 first_camera_position(const Vec3& pos, std::int16_t yaw, const CameraConfig& config) {
    Vec3 out;
    out.x = pos.x - fx::mul(fx::sin(yaw), config.first_camera_distance);
    out.y = pos.y - config.first_camera_height;
    out.z = pos.z - fx::mul(fx::cos(yaw), config.first_camera_distance);
    return out;
}

Vec3 offset_position(const Vec3& pos, std::int32_t xz_radius, std::int32_t yaw, std::int32_t dy) {
    Vec3 out;
    out.x = pos.x + fx::mul(xz_radius, fx::sin(yaw));
    out.z = pos.z + fx::mul(xz_radius, fx::cos(yaw));
    out.y = pos.y + dy;
    return out;
}

Vec3 head_point(const Matrix& head_world, const SubjectRules& rules) {
    math::Matrix3 rot;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            rot.m[i][j] = head_world.m[i][j];
        }
    }
    const math::MatVec p =
        math::matrix_vector(rot, math::Vec3s{0, rules.head_bone_offset_y, 0}, math::Offset::None, math::Vec3i{});
    Vec3 out;
    out.x = q8_to_q12(p.full.x + head_world.t[0]);
    out.y = q8_to_q12(p.full.y + head_world.t[1]) + rules.head_offset_y;
    out.z = q8_to_q12(p.full.z + head_world.t[2]);
    return out;
}

SubjectFrame subject_of(const SubjectSource& src, const SubjectRules& rules) {
    SubjectFrame out;
    Subject& s = out.subject;
    s.position = src.position;
    if (src.connected) {
        s.ground_y = rules.connected_ground_y;
        s.head = Vec3{src.position.x, src.position.y + rules.connected_head_y, src.position.z};
    } else {
        // The world answers "no ground here" with a fixed height; the camera then uses the origin.
        s.ground_y = src.ground_height == rules.ground_missing_y ? 0 : src.ground_height;
        s.head = src.head;
    }
    s.top_y = src.position.y + rules.top_offset_y;
    s.bottom_y = src.position.y + fx::mul(src.elevation_rate, rules.bottom_offset_y);
    out.inhibit_far_look = src.elevation_rate > 0;
    // Speed, turn speed and yaw pass through the camera's 16-bit fields.
    s.move_speed = static_cast<std::int16_t>(src.move_speed);
    s.move_angle_y = src.heading;
    s.angle_speed_y = static_cast<std::int16_t>(src.turn_speed);
    s.eye_angle_y = static_cast<std::int16_t>(src.yaw);
    s.eye_angle_wy = rules.eye_half_angle;
    s.watch_xz_radius = rules.watch_radius;
    return out;
}

Vec3 adjust_orbit(Orbit& orbit, const OrbitInput& in, std::int32_t vblanks, const Vec3& subject_pos,
                  std::int32_t subject_yaw, const DebugCameraRules& rules) {
    const std::int32_t turn = vblanks * rules.turn_per_vblank;
    if (in.closer) {
        orbit.radius -= rules.orbit_step;
    }
    if (in.farther) {
        orbit.radius += rules.orbit_step;
    }
    if (in.turn_right) {
        orbit.yaw -= turn;
    }
    if (in.turn_left) {
        orbit.yaw += turn;
    }
    if (in.up) {
        orbit.height -= rules.orbit_step;
    }
    if (in.down) {
        orbit.height += rules.orbit_step;
    }
    if (orbit.radius < rules.orbit_min_radius) {
        orbit.radius = rules.orbit_min_radius;
    }
    return offset_position(subject_pos, rules.reference_ahead, subject_yaw, rules.reference_y);
}

FlyResult fly_by_pad(const Vec3& view_pos, const Angles& view_angles, const FlyInput& in, std::int32_t vblanks,
                     const DebugCameraRules& rules) {
    const std::int32_t step = in.boost ? rules.fly_step * rules.fly_boost : rules.fly_step;
    const std::int32_t turn = vblanks * rules.turn_per_vblank;
    Vec3 pos{q12_to_q8(view_pos.x), q12_to_q8(view_pos.y), q12_to_q8(view_pos.z)};
    Angles ang = view_angles;

    // A backward step is one unit longer: the game rounds it down where the scale truncates.
    if (!in.shift) {
        if (in.down) {
            ang.x = static_cast<std::int16_t>(ang.x - turn);
        }
        if (in.up) {
            ang.x = static_cast<std::int16_t>(ang.x + turn);
        }
        if (in.right) {
            ang.y = static_cast<std::int16_t>(ang.y + turn);
        }
        if (in.left) {
            ang.y = static_cast<std::int16_t>(ang.y - turn);
        }
        if (in.forward || in.backward) {
            std::int32_t move = 0;
            if (in.forward) {
                move = step;
            }
            if (in.backward) {
                move = -step - 1;
            }
            pos.x += mul_div(move, fx::sin(ang.y));
            pos.z += mul_div(move, fx::cos(ang.y));
        }
    } else {
        if (in.up) {
            pos.y -= step;
        }
        if (in.down) {
            pos.y += step;
        }
        if (in.right || in.left) {
            std::int32_t move = 0;
            if (in.right) {
                move = step;
            }
            if (in.left) {
                move = -step - 1;
            }
            pos.x += mul_div(move, fx::sin(ang.y + fx::angle(90.0f)));
            pos.z += mul_div(move, fx::cos(ang.y + fx::angle(90.0f)));
        }
    }

    FlyResult out;
    const math::Matrix3 rot = math::rot_matrix_zxy_neg(math::Vec3s{ang.x, ang.y, ang.z});
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            out.view.m[i][j] = rot.m[i][j];
        }
    }
    out.view.t[0] = pos.x;
    out.view.t[1] = pos.y;
    out.view.t[2] = pos.z;

    out.moved = in.up || in.down || in.left || in.right || in.forward || in.backward;
    if (out.moved) {
        std::int16_t ahead[3];
        direction_from_angles(ang.x, ang.y, rules.fly_reference_distance, ahead);
        out.reference = Vec3{q8_to_q12(pos.x + ahead[0]), q8_to_q12(pos.y + ahead[1]), q8_to_q12(pos.z + ahead[2])};
        out.orbit.yaw = fx::wrap_angle(ang.y + fx::angle(180.0f));
        out.orbit.height = q8_to_q12(-ahead[1]);
        out.orbit.radius = q8_to_q12(world::game_sqrt(ahead[0] * ahead[0] + ahead[2] * ahead[2]));
    }
    return out;
}

Vec3 listener_direction(const Vec3& listener, const Vec3& source) {
    const math::Vec3i offset{(source.x - listener.x) >> 6, (source.y - listener.y) >> 6, (source.z - listener.z) >> 6};
    const math::Vec3i dir = math::vector_normal(offset);
    return Vec3{dir.x, dir.y, dir.z};
}

}  // namespace sh::camera
