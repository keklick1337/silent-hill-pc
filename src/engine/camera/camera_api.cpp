// The camera API between frames (camera_rig.h): what scripts, map events and the camera driver
// set on the camera state outside the frame update. Each call only writes the fields it names.

#include "engine/camera/camera_math.h"
#include "engine/camera/camera_rig.h"

namespace sh::camera {

namespace {

constexpr std::uint32_t kWarpAll =
    camera_flags::warp_camera | camera_flags::warp_watch | camera_flags::warp_camera_target;

}  // namespace

void CameraRig::init_system(std::int32_t watch_max_y) {
    state_.view_active = false;
    state_.current_road = fallback_near_road();
    state_.old_cam_exclusion_radius = -1;
    state_.watch_target_max_y = watch_max_y;
    state_.update_look_at_point = false;
    state_.update_look_at_matrix = false;
}

void CameraRig::start_system(std::int16_t screen_distance) {
    state_.view_active = true;
    state_.update_look_at_point = false;
    state_.update_look_at_matrix = false;
    state_.geom_screen_dist = screen_distance;
}

void CameraRig::set_first_camera(const Vec3& pos, std::int16_t eye_angle_y, bool through_door) {
    state_.cam_rotation_offset_speed = Angles{};
    state_.flags = kWarpAll;
    state_.cam_pos = pos;
    state_.cam_target_pos = pos;
    state_.cam_velocity = Vec3{};
    state_.cam_target_velocity = Vec3{};
    state_.cam_move_angle_y = 0;
    state_.cam_target_speed = 0;
    // The camera starts behind the subject: the ideal direction is the opposite of its look.
    state_.cam_to_ideal_angle_y = static_cast<std::int16_t>(fx::wrap_angle(eye_angle_y + fx::angle(180.0f)));
    state_.current_road = fallback_near_road();
    state_.through_door_activate = through_door;
    state_.through_door.active = false;
    state_.through_door.timer = 0;
}

void CameraRig::set_flags(std::uint32_t enable, std::uint32_t disable) {
    state_.flags = (state_.flags | enable) & ~disable;
}

std::int32_t CameraRig::set_watch_max_y(std::int32_t max_y) {
    const std::int32_t previous = state_.watch_target_max_y;
    state_.watch_target_max_y = max_y;
    return previous;
}

void CameraRig::user_watch_target(const Vec3& target, const WatchMoveParams& params, bool warp) {
    state_.flags = (state_.flags & ~(camera_flags::user_watch | camera_flags::character_visible)) |
                   camera_flags::user_watch;
    if (warp) {
        state_.flags |= camera_flags::warp_watch;
    }
    state_.watch_target_pos = target;
    state_.watch_target_roll = 0;
    state_.user_watch_params = params;
}

void CameraRig::user_camera_target(const Vec3& target, const CamMoveParams& params, bool warp) {
    state_.flags = (state_.flags & ~(camera_flags::user_camera | camera_flags::warp_camera)) |
                   camera_flags::user_camera;
    if (warp) {
        state_.flags |= camera_flags::warp_camera;
    }
    state_.cam_target_pos = target;
    state_.user_cam_params = params;
}

void CameraRig::return_to_auto_camera(bool warp, bool keep_projection, std::int16_t screen_distance) {
    if (warp) {
        state_.flags |= kWarpAll;
    }
    state_.flags &= ~(camera_flags::user_camera | camera_flags::user_watch);
    if (!keep_projection) {
        state_.geom_screen_dist = screen_distance;
    }
}

void CameraRig::set_look_at_matrix(const Matrix& m) {
    state_.update_look_at_matrix = true;
    state_.look_at_matrix = m;
}

void CameraRig::set_subject(const Subject& s) {
    state_.chara_pos = s.position;
    state_.chara_bottom_y = s.bottom_y;
    state_.chara_top_y = s.top_y;
    // Midpoint with the 32-bit wraparound of the sum, halved by an arithmetic shift.
    state_.chara_center_y = static_cast<std::int32_t>(static_cast<std::uint32_t>(s.bottom_y) +
                                                      static_cast<std::uint32_t>(s.top_y)) >> 1;
    state_.chara_ground_y = s.ground_y;
    state_.chara_head_pos = s.head;
    state_.chara_move_speed = s.move_speed;
    state_.chara_move_angle_y = static_cast<std::int16_t>(s.move_angle_y);
    state_.chara_angle_speed_y = static_cast<std::int16_t>(s.angle_speed_y);
    state_.chara_eye_angle_y = static_cast<std::int16_t>(s.eye_angle_y);
    state_.chara_eye_angle_wy = static_cast<std::int16_t>(s.eye_angle_wy);
    state_.chara_watch_xz_radius = s.watch_xz_radius;
}

}  // namespace sh::camera
