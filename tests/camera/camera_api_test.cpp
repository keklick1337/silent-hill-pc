// The camera API, the driver rules and camera.json against hand-worked numbers that pin the rules.

#include <fstream>
#include <sstream>
#include <string>

#include "engine/camera/camera_config.h"
#include "engine/camera/camera_driver.h"
#include "engine/camera/camera_rig.h"

#include "framework/sh_test.h"

namespace cam = sh::camera;

namespace {

std::string read_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool same(const cam::DebugCameraRules& a, const cam::DebugCameraRules& b) {
    return a.start_offset_x == b.start_offset_x && a.start_y == b.start_y && a.start_radius == b.start_radius &&
           a.orbit_step == b.orbit_step && a.orbit_min_radius == b.orbit_min_radius &&
           a.turn_per_vblank == b.turn_per_vblank && a.reference_ahead == b.reference_ahead &&
           a.reference_y == b.reference_y && a.fly_step == b.fly_step && a.fly_boost == b.fly_boost &&
           a.fly_reference_distance == b.fly_reference_distance && a.head_reference.x == b.head_reference.x &&
           a.head_reference.y == b.head_reference.y && a.head_reference.z == b.head_reference.z &&
           a.head_orbit_yaw == b.head_orbit_yaw && a.head_orbit_roll == b.head_orbit_roll &&
           a.head_orbit_height == b.head_orbit_height && a.head_orbit_radius == b.head_orbit_radius;
}

bool same(const cam::SubjectRules& a, const cam::SubjectRules& b) {
    return a.top_offset_y == b.top_offset_y && a.bottom_offset_y == b.bottom_offset_y &&
           a.connected_ground_y == b.connected_ground_y && a.connected_head_y == b.connected_head_y &&
           a.ground_missing_y == b.ground_missing_y && a.eye_half_angle == b.eye_half_angle &&
           a.watch_radius == b.watch_radius && a.head_bone_offset_y == b.head_bone_offset_y &&
           a.head_offset_y == b.head_offset_y;
}

}  // namespace

// The shipped file holds exactly the built-in numbers (the original's constants, converted by the game's rule).
SH_TEST(camera_api_shipped_config_is_the_builtin) {
    const std::string text = read_text(std::string(SH_PROJECT_ROOT) + "/gamedata/config/camera.json");
    SH_CHECK(!text.empty());
    const auto parsed = cam::parse_camera_config(text);
    SH_CHECK(parsed);
    const cam::CameraConfig& c = parsed.value();
    const cam::CameraConfig d{};
    SH_CHECK(c.draw_cull_margin == d.draw_cull_margin);
    SH_CHECK(c.default_watch.accel_x == 4915 && c.default_watch.accel_y == 9011);
    SH_CHECK(c.default_watch.max_speed_x == 901 && c.default_watch.max_speed_y == 2867);
    SH_CHECK(c.user_camera.accel_xz == 40960 && c.user_camera.accel_y == 12288);
    SH_CHECK(c.user_camera.max_speed_xz == 24576 && c.user_camera.max_speed_y == 8192);
    SH_CHECK(c.watch_max_y == 122880);
    SH_CHECK(c.first_camera_distance == 6144 && c.first_camera_height == 6963);
    SH_CHECK(same(c.subject, d.subject));
    SH_CHECK(same(c.debug, d.debug));
    SH_CHECK(c.subject.head_bone_offset_y == -25 && c.subject.eye_half_angle == 1365);
    SH_CHECK(c.debug.head_orbit_yaw == 1877 && c.debug.head_reference.y == -614);
    SH_CHECK(!cam::parse_camera_config(R"({"camera": {"watch_max_y": "far"}})"));
    SH_CHECK(!cam::parse_camera_config(R"({"debug_camera": {"fly_boost": 0}})"));
}

SH_TEST(camera_api_setters) {
    cam::CameraRig rig;
    cam::CameraState& st = rig.state();
    st.view_active = true;
    st.update_look_at_point = true;
    rig.init_system(122880);
    SH_CHECK(!st.view_active && !st.update_look_at_point && st.old_cam_exclusion_radius == -1);
    SH_CHECK(st.current_road.road == cam::kDefaultRoad && st.current_road.road_area.max_x == 7680);
    rig.start_system(240);
    SH_CHECK(st.view_active && st.geom_screen_dist == 240);

    st.flags = cam::camera_flags::character_visible | cam::camera_flags::view_button;
    rig.user_watch_target({1, 2, 3}, {10, 20, 30, 40}, true);
    SH_CHECK(st.flags == (cam::camera_flags::view_button | cam::camera_flags::user_watch | cam::camera_flags::warp_watch));
    SH_CHECK(st.watch_target_pos.y == 2 && st.user_watch_params.max_speed_y == 40);
    rig.user_camera_target({4, 5, 6}, {1, 2, 3, 4}, false);
    SH_CHECK((st.flags & cam::camera_flags::user_camera) && !(st.flags & cam::camera_flags::warp_camera));
    rig.return_to_auto_camera(true, true, 100);
    SH_CHECK(!(st.flags & (cam::camera_flags::user_camera | cam::camera_flags::user_watch)));
    SH_CHECK((st.flags & cam::camera_flags::warp_camera_target) && st.geom_screen_dist == 240);
    rig.return_to_auto_camera(false, false, 100);
    SH_CHECK(st.geom_screen_dist == 100);
    SH_CHECK(rig.set_watch_max_y(5) == 122880 && st.watch_target_max_y == 5);

    // Facing 90 degrees: the camera's ideal direction is the opposite, -90.
    rig.set_first_camera({7, 8, 9}, 1024, true);
    SH_CHECK(st.flags == (cam::camera_flags::warp_camera | cam::camera_flags::warp_watch |
                          cam::camera_flags::warp_camera_target));
    SH_CHECK(st.cam_to_ideal_angle_y == -1024 && st.cam_target_pos.z == 9 && st.through_door_activate);

    cam::Subject s;
    s.bottom_y = -100;
    s.top_y = -7001;
    s.move_speed = 70000;  // passes through a 16-bit field
    rig.set_subject(s);
    SH_CHECK(st.chara_center_y == -3551);
    SH_CHECK(st.chara_move_speed == 70000);
}

SH_TEST(camera_driver_rules) {
    const cam::CameraConfig c{};
    // Facing +Z (yaw 0): the camera sits 1.5 m toward -Z and 1.7 m up.
    const cam::Vec3 p = cam::first_camera_position({1000, 0, 1000}, 0, c);
    SH_CHECK(p.x == 1000 && p.y == -6963 && p.z == 1000 - 6144);

    cam::SubjectSource src;
    src.position = {0, 4096, 0};
    src.ground_height = 32768;  // the world's "no ground" height
    src.elevation_rate = 4096;
    src.move_speed = 70000;
    const cam::SubjectFrame f = cam::subject_of(src, c.subject);
    SH_CHECK(f.subject.ground_y == 0 && f.inhibit_far_look);
    SH_CHECK(f.subject.top_y == 4096 - 6963 && f.subject.bottom_y == 4096 - 2048);
    SH_CHECK(f.subject.move_speed == static_cast<std::int16_t>(70000) && f.subject.eye_angle_wy == 1365);
    src.connected = true;
    src.elevation_rate = 0;
    const cam::SubjectFrame g = cam::subject_of(src, c.subject);
    SH_CHECK(g.subject.ground_y == -8192 && g.subject.head.y == 4096 - 7782 && !g.inhibit_far_look);

    cam::Orbit o{4200, 0, 0};
    cam::OrbitInput in;
    in.closer = true;
    in.turn_left = true;
    const cam::Vec3 ref = cam::adjust_orbit(o, in, 2, {0, 0, 0}, 0, c.debug);
    SH_CHECK(o.radius == 4096 && o.yaw == 22);  // clamped to the 1 m minimum
    SH_CHECK(ref.x == 0 && ref.z == 2048 && ref.y == -4096);

    cam::Matrix head{};
    head.m[0][0] = head.m[1][1] = head.m[2][2] = 4096;
    head.t[1] = -256;  // 1 m up, Q8
    const cam::Vec3 h = cam::head_point(head, c.subject);
    SH_CHECK(h.x == 0 && h.y == ((-256 - 25) << 4) - 1228 && h.z == 0);
}
