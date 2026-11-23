// The camera rig's stage B helpers and parameter choice against hand-worked numbers.

#include "engine/camera/camera_motion.h"
#include "engine/camera/camera_rig.h"

#include "framework/sh_test.h"

namespace cam = sh::camera;

SH_TEST(camera_piecewise_linear_ends_and_middle) {
    const std::int32_t table[] = {0, 100, 200};
    // Three values over [0, 100]: intervals of 50.
    SH_CHECK(cam::piecewise_linear(table, 3, -1, 0, 100) == 0);
    SH_CHECK(cam::piecewise_linear(table, 3, 100, 0, 100) == 200);
    SH_CHECK(cam::piecewise_linear(table, 3, 25, 0, 100) == 50);
    SH_CHECK(cam::piecewise_linear(table, 3, 75, 0, 100) == 150);
}

SH_TEST(camera_speed_toward_accelerates_then_arrives) {
    // Half a second at 1.0/s^2 would give 0.5/s, but 1000 units from the target with a brake of 1.0
    // per unit allows only 1000.
    SH_CHECK(cam::speed_toward(0, 0, 1000, 4096, 8192, 4096, 2048) == 1000);
    SH_CHECK(cam::speed_toward(0, 1000, 0, 4096, 8192, 4096, 2048) == -1000);
    // Far away the speed cap wins.
    SH_CHECK(cam::speed_toward(0, 0, 100000, 40960, 4096, 4096, 4096) == 4096);
}

SH_TEST(camera_angular_speed_wraps_the_gap) {
    // From 4000 to 100 is +196 across the wrap, so the speed goes positive.
    SH_CHECK(cam::angular_speed_toward(0, 4000, 100, 4096, 8192, 4096, 4096) == 196);
}

SH_TEST(camera_follow_warp_snaps_to_target) {
    cam::CameraRig rig;
    rig.set_roads({}, false);
    cam::CameraState& st = rig.state();
    st.flags = cam::camera_flags::user_camera | cam::camera_flags::warp_camera;
    st.cam_target_pos = cam::Vec3{4096, 0, 0};
    st.cam_velocity = cam::Vec3{5, 6, 7};
    cam::FrameInput in;
    in.delta_time = 136;
    const cam::StageBResult r = rig.update_target(in, cam::StageAResult{});
    SH_CHECK(st.cam_pos.x == 4096 && st.cam_pos.y == 0 && st.cam_pos.z == 0);
    SH_CHECK(st.cam_velocity.x == 0 && st.cam_velocity.y == 0 && st.cam_velocity.z == 0);
    SH_CHECK(st.cam_move_angle_y == 1024);  // +X is a quarter turn
    // A script owns the camera, so its own motion parameters are used.
    SH_CHECK(r.cam_params.accel_xz == st.user_cam_params.accel_xz);
}

SH_TEST(camera_watch_params_blend_by_self_view_rate) {
    cam::CameraRig rig;
    rig.set_roads({}, false);
    cam::CameraState& st = rig.state();
    st.flags = cam::camera_flags::user_camera | cam::camera_flags::warp_camera;
    cam::FrameInput in;
    cam::StageAResult a;

    a.self_view_rate = 0;
    cam::StageBResult r = rig.update_target(in, a);
    SH_CHECK(r.watch_params.accel_x == 4915 && r.watch_params.accel_y == 9011);
    SH_CHECK(r.watch_params.max_speed_x == 901 && r.watch_params.max_speed_y == 2867);

    a.self_view_rate = 4096;
    st.chara_move_speed = 4096;  // running adds one turn per second squared to the yaw acceleration
    r = rig.update_target(in, a);
    SH_CHECK(r.watch_params.accel_x == 6881 && r.watch_params.accel_y == 12615 + 4096);
    SH_CHECK(r.watch_params.max_speed_x == 1802 && r.watch_params.max_speed_y == 5734);
    SH_CHECK(st.blended_watch_params.accel_y == 12615 + 4096);
}
