// The game camera (game_camera.h): one persistent rig whose state carries across frames and API
// calls, and motion defaults that start as the built-in camera.json numbers.

#include "engine/camera/camera_config.h"
#include "engine/camera/game_camera.h"

#include "framework/sh_test.h"

namespace cam = sh::camera;

SH_TEST(game_camera_is_one_persistent_object) {
    cam::GameCamera& a = cam::game_camera();
    cam::GameCamera& b = cam::game_camera();
    SH_CHECK(&a == &b);
    SH_CHECK(&a.state() == &a.rig.state());
}

SH_TEST(game_camera_motion_defaults_start_built_in) {
    const cam::CameraConfig built_in{};
    const cam::GameCamera& c = cam::game_camera();
    SH_CHECK(c.default_watch.accel_x == built_in.default_watch.accel_x);
    SH_CHECK(c.default_watch.max_speed_y == built_in.default_watch.max_speed_y);
    SH_CHECK(c.user_camera.accel_xz == built_in.user_camera.accel_xz);
    SH_CHECK(c.user_camera.max_speed_y == built_in.user_camera.max_speed_y);
}

SH_TEST(game_camera_state_survives_between_calls_and_frames) {
    cam::GameCamera& c = cam::game_camera();
    const cam::CameraState saved = c.state();
    c.rig.set_roads({}, false);
    c.rig.init_system(4096);
    c.rig.start_system(240);
    c.rig.set_first_camera(cam::Vec3{4096, -8192, 4096}, 0, false);
    cam::Subject s;
    s.position = cam::Vec3{0, 0, 0};
    s.top_y = -6963;
    c.rig.set_subject(s);
    SH_CHECK(c.state().view_active);
    SH_CHECK(c.state().geom_screen_dist == 240);

    cam::FrameInput in;
    in.delta_time = 136;
    in.screen_width = 320;
    in.screen_height = 240;
    c.rig.update(in);
    // The warp flags of the first camera are consumed by the frame and stay consumed.
    SH_CHECK((c.state().flags & cam::camera_flags::warp_camera) == 0);
    c.rig.update(in);
    SH_CHECK(cam::game_camera().state().self_view_timer == c.state().self_view_timer);
    c.rig.stop_system();
    SH_CHECK(!cam::game_camera().state().view_active);
    c.state() = saved;
}
