// The free camera of the developer tools (engine/devtools/free_camera.h):
// start, look, flight with easing, the wheel speed scale, steady and cruise, and the view.

#include "engine/devtools/free_camera.h"

#include "framework/sh_test.h"

namespace {

namespace dt = sh::devtools;

dt::FreeCameraInput frame() {
    dt::FreeCameraInput in;
    in.delta_time = 68;  // a 60 fps frame
    return in;
}

}  // namespace

SH_TEST(devtools_free_camera_start_and_look) {
    dt::FreeCamera cam;
    cam.start({100, -200, 300}, 4096 + 1024);
    SH_CHECK(cam.yaw == 5120);  // kept as given; the next update wraps it
    dt::FreeCameraInput in = frame();
    in.keys.hud = true;  // held since before the start: no toggle
    in.mouse_dx = 10;
    in.mouse_dy = -5;
    cam.update(in);
    SH_CHECK(!cam.hud_hidden);
    SH_CHECK(cam.yaw == 1024 + 60);
    SH_CHECK(cam.pitch == -20);
    SH_CHECK(cam.position[0] == 100 && cam.position[1] == -200 && cam.position[2] == 300);
    in = frame();
    cam.update(in);
    in.keys.hud = true;
    cam.update(in);
    SH_CHECK(cam.hud_hidden);
}

SH_TEST(devtools_free_camera_flight) {
    dt::FreeCamera cam;
    cam.start({0, 0, 0}, 0);
    dt::FreeCameraInput in = frame();
    in.keys.forward = true;
    for (int i = 0; i < 120; i++) {
        cam.update(in);
    }
    // Yaw 0 flies along +Z, eased up to 640 units a 60 fps frame.
    SH_CHECK(cam.position[0] == 0);
    SH_CHECK(cam.position[2] > 60000);
    in.keys.forward = false;
    for (int i = 0; i < 120; i++) {
        cam.update(in);
    }
    const int z = cam.position[2];
    cam.update(in);
    SH_CHECK(cam.position[2] - z < 2);  // eased to a stop
    // The pitch limit.
    in.mouse_dy = 1000;
    cam.update(in);
    SH_CHECK(cam.pitch == 900);
}

SH_TEST(devtools_free_camera_speed_and_cruise) {
    dt::FreeCamera cam;
    cam.start({0, 0, 0}, 0);
    dt::FreeCameraInput in = frame();
    in.wheel_notches = 3;
    cam.update(in);
    SH_CHECK(cam.speed_scale > 1.95f && cam.speed_scale < 1.96f);
    in.wheel_notches = -3;
    cam.update(in);
    SH_CHECK(cam.speed_scale == 1.0f);
    in.wheel_notches = 100;
    cam.update(in);
    SH_CHECK(cam.speed_scale == 32.0f);
    // Cruise keeps flying without input.
    in = frame();
    cam.update(in);
    in.keys.cruise = true;
    in.keys.rise = true;
    cam.update(in);
    SH_CHECK(cam.cruise);
    in.keys.cruise = false;
    in.keys.rise = false;
    for (int i = 0; i < 60; i++) {
        cam.update(in);
    }
    const int y = cam.position[1];
    cam.update(in);
    SH_CHECK(cam.position[1] < y);  // still rising (-Y is up)
}

SH_TEST(devtools_free_camera_look_at) {
    dt::FreeCamera cam;
    cam.start({1000, 2000, 3000}, 0);
    const auto at = cam.look_at();
    SH_CHECK(at[0] == 1000);
    SH_CHECK(at[1] == 2000);
    SH_CHECK(at[2] == 3000 + 20480);
    cam.yaw = 1024;
    const auto right = cam.look_at();
    SH_CHECK(right[0] == 1000 + 20480);
    SH_CHECK(right[2] == 3000);
}
