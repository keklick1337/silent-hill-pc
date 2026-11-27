// The look of the PC alternate cameras (engine/camera/alt_camera.h) between whole angle units and between two
// updates, with the shipped tuning: a slow mouse or stick is not lost to rounding, the turn does not depend on the
// frame rate, the unrounded view (AltCamera::pose) is the game's own view, and the angles of a frame presented
// between two updates (AltCamera::look_ahead) lead evenly to the next update's.

#include "engine/camera/alt_camera.h"

#include "framework/sh_test.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

namespace {

namespace cam = sh::camera;

cam::AltCameraTuning shipped() {
    std::ifstream in(std::string(SH_PROJECT_ROOT) + "/gamedata/config/alt_cameras.json", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto parsed = cam::parse_alt_camera_tuning(text);
    if (!parsed) {
        SH_SKIP(parsed.error().c_str());
    }
    return parsed.value();
}

class Open : public cam::AltCameraHost {
public:
    bool ray_hit(const cam::AltVec3&, const cam::AltVec3&, std::int32_t*) override { return false; }
};

// Every ray stops after `hit` units.
class Close : public cam::AltCameraHost {
public:
    std::int32_t hit = 4096;
    bool ray_hit(const cam::AltVec3&, const cam::AltVec3&, std::int32_t* distance) override {
        *distance = hit;
        return true;
    }
};

constexpr std::int32_t kFrame60 = 4096 / 60;  // Q12 seconds

cam::AltCameraInput input(cam::AltStyle style) {
    cam::AltCameraInput in;
    in.style = style;
    in.delta_time = kFrame60;
    in.in_gameplay = true;
    in.options.follow_smoothing = 0.0f;
    in.options.recenter = false;
    in.options.head_tracking = false;
    in.options.eye_offset = cam::AltVec3{-29, -6836, 919};
    in.head.ty = -6836 >> 4;
    return in;
}

// The update's own angles carried into the next one, as the host does.
cam::AltCameraOutput step(cam::AltCamera& c, cam::AltCameraInput& in, cam::AltCameraHost& host) {
    const cam::AltCameraOutput out = c.update(in, host);
    in.yaw = out.yaw;
    in.pitch = out.pitch;
    return out;
}

double angle_between(const cam::AltPose& p, const cam::AltVec3& eye, const cam::AltVec3& at) {
    const double a[3] = {p.at[0] - p.eye[0], p.at[1] - p.eye[1], p.at[2] - p.eye[2]};
    const double b[3] = {double(at.x - eye.x), double(at.y - eye.y), double(at.z - eye.z)};
    const double dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    const double la = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    const double lb = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
    const double c = dot / (la * lb);
    return std::acos(c > 1.0 ? 1.0 : c) * 180.0 / 3.14159265358979323846;
}

double eye_gap(const cam::AltPose& p, const cam::AltVec3& eye) {
    return std::sqrt((p.eye[0] - eye.x) * (p.eye[0] - eye.x) + (p.eye[1] - eye.y) * (p.eye[1] - eye.y) +
                     (p.eye[2] - eye.z) * (p.eye[2] - eye.z));
}

}  // namespace

// A mouse that moves one count a frame at sensitivity 0.1 asks for 0.6 angle units a frame across and 0.2 down.
// A second of it turns the view by 36 and 12 units in both styles; the fraction of a frame is carried, not dropped.
SH_TEST(camera_alt_look_keeps_a_slow_mouse) {
    for (const cam::AltStyle style : {cam::AltStyle::FirstPerson, cam::AltStyle::ThirdPerson}) {
        cam::AltCamera c(shipped());
        Open level;
        cam::AltCameraInput in = input(style);
        in.options.mouse_sensitivity = 0.1f;
        step(c, in, level);
        in.mouse_dx = 1.0;
        in.mouse_dy = -1.0;
        cam::AltCameraOutput out;
        for (int i = 0; i < 60; ++i) out = step(c, in, level);
        SH_CHECK(out.yaw == 36);
        SH_CHECK(out.pitch == 12);
    }
}

// The same hand movement arrives as 2.4 counts a frame at 60 frames a second and as 1 count a frame at 144:
// the view ends at the same angle (2 seconds, 288 counts, 1728 units less the turns of a full circle).
SH_TEST(camera_alt_look_mouse_is_frame_rate_independent) {
    for (const cam::AltStyle style : {cam::AltStyle::FirstPerson, cam::AltStyle::ThirdPerson}) {
        std::int32_t yaw[2] = {0, 0};
        const int frames[2] = {120, 288};
        const std::int32_t delta[2] = {4096 / 60, 4096 / 144};
        for (int k = 0; k < 2; ++k) {
            cam::AltCamera c(shipped());
            Open level;
            cam::AltCameraInput in = input(style);
            in.delta_time = delta[k];
            step(c, in, level);
            in.mouse_dx = 288.0 / frames[k];
            cam::AltCameraOutput out;
            for (int i = 0; i < frames[k]; ++i) {
                // The body turns with the view, as while Harry moves: the shoulders do not stop it.
                in.harry_yaw = in.yaw;
                out = step(c, in, level);
            }
            yaw[k] = out.yaw;
        }
        SH_CHECK(yaw[0] == 1728);
        SH_CHECK(std::abs(yaw[0] - yaw[1]) <= 1);
    }
}

// First person: the right stick half over (64 of 128) turns 20 units per 30 fps frame, 600 a second, at any
// frame rate; the steps of 30, 60 and 144 frames a second (136, 68 and 28) are whole Q12 times, so a second of each
// differs by its rounding only.
SH_TEST(camera_alt_first_person_stick_is_frame_rate_independent) {
    const std::int32_t delta[3] = {136, 68, 28};
    for (const std::int32_t d : delta) {
        cam::AltCamera c(shipped());
        Open level;
        cam::AltCameraInput in = input(cam::AltStyle::FirstPerson);
        in.delta_time = d;
        step(c, in, level);
        in.right_x = 128 + 64;
        cam::AltCameraOutput out;
        const int frames = 4080 / d;  // one second, to the frame
        for (int i = 0; i < frames; ++i) {
            in.harry_yaw = in.yaw;
            out = step(c, in, level);
        }
        const double turned = 20.0 * frames * d / 136.0;
        SH_CHECK(std::fabs(out.yaw - turned) <= 1.0);
    }
}

// With no look input nothing moves and nothing is owed: the angles stay whole, the view is the same every frame.
SH_TEST(camera_alt_look_idle_changes_nothing) {
    for (const cam::AltStyle style : {cam::AltStyle::FirstPerson, cam::AltStyle::ThirdPerson}) {
        cam::AltCamera c(shipped());
        Open level;
        cam::AltCameraInput in = input(style);
        in.yaw = 777;
        in.harry_yaw = 700;
        in.pitch = -55;
        const cam::AltCameraOutput first = step(c, in, level);
        for (int i = 0; i < 200; ++i) {
            const cam::AltCameraOutput out = step(c, in, level);
            SH_CHECK(out.yaw == 777 && out.pitch == -55);
            SH_CHECK(out.eye == first.eye && out.look_at == first.look_at);
            SH_CHECK(out.basis.yaw == 777.0 && out.basis.pitch == -55.0);
            const cam::AltLook ahead = c.look_ahead(in, 0.01);
            SH_CHECK(ahead.yaw == 777.0 && ahead.pitch == -55.0);
        }
    }
}

// The unrounded view is the game's view: along the update's own angles it stands within two millimetres (8 units:
// the game's sine table and truncated products over the 2.5 m arm) of the rounded eye and looks within 0.03 degrees of
// the rounded direction - in the open, aiming over the shoulder, with the rear look, and with a wall holding the eye
// in.
SH_TEST(camera_alt_pose_is_the_rounded_view) {
    const cam::AltCameraTuning t = shipped();
    Open open;
    Close wall;
    cam::AltCameraHost* hosts[2] = {&open, &wall};
    for (cam::AltCameraHost* host : hosts) {
        for (int k = 0; k < 24; ++k) {
            cam::AltCamera c(t);
            cam::AltCameraInput in = input(cam::AltStyle::OverShoulder);
            in.harry = cam::AltVec3{40960 + k * 517, -300, -8192};
            in.harry_yaw = k * 170;
            in.yaw = (k * 613) & 4095;
            in.pitch = -400 + k * 37;
            in.aiming = (k & 1) != 0;
            in.rear_look = (k % 5) == 4;
            in.shoulder_side = (k & 2) ? 1 : -1;
            in.options.rest_x = 2252;
            in.options.aim_x = 3686;
            in.options.aim_y = -200;
            const cam::AltCameraOutput out = c.update(in, *host);
            SH_CHECK(out.basis.valid && !out.basis.first_person);
            const cam::AltPose p = cam::AltCamera::pose(out.basis, out.basis.yaw, out.basis.pitch);
            SH_CHECK(eye_gap(p, out.eye) < 8.0);
            SH_CHECK(angle_between(p, out.eye, out.look_at) < 0.03);
        }
    }
    for (int k = 0; k < 24; ++k) {
        cam::AltCamera c(t);
        cam::AltCameraInput in = input(cam::AltStyle::FirstPerson);
        in.harry = cam::AltVec3{4096 * k, 0, 8192};
        in.head.tx = in.harry.x >> 4;
        in.head.tz = in.harry.z >> 4;
        in.harry_yaw = k * 170;
        in.yaw = in.harry_yaw + (k * 83) % 1000 - 500;
        in.pitch = -800 + k * 67;
        const cam::AltCameraOutput out = c.update(in, open);
        SH_CHECK(out.basis.valid && out.basis.first_person);
        const cam::AltPose p = cam::AltCamera::pose(out.basis, out.basis.yaw, out.basis.pitch);
        SH_CHECK(eye_gap(p, out.eye) < 8.0);
        SH_CHECK(angle_between(p, out.eye, out.look_at) < 0.03);
    }
}

// A frame presented between two updates looks where the next update will: with the counts the mouse sent since
// the last update and the stick held, the angles a whole step ahead are the next update's own, and asking for
// them changes nothing in the camera. Without and with look smoothing, and in first person.
SH_TEST(camera_alt_look_ahead_meets_the_next_update) {
    struct Case {
        cam::AltStyle style;
        float smoothing;
    };
    const Case cases[] = {
        {cam::AltStyle::ThirdPerson, 0.0f}, {cam::AltStyle::ThirdPerson, 0.6f}, {cam::AltStyle::FirstPerson, 0.0f}};
    for (const Case& k : cases) {
        cam::AltCamera c(shipped());
        cam::AltCamera untouched(shipped());
        Open level;
        cam::AltCameraInput in = input(k.style);
        in.options.look_smoothing = k.smoothing;
        in.options.mouse_sensitivity = 0.3f;
        in.right_x = 128 + 90;
        in.right_y = 128 - 70;
        cam::AltCameraInput twin = in;
        step(c, in, level);
        step(untouched, twin, level);
        for (int i = 0; i < 40; ++i) {
            in.mouse_dx = twin.mouse_dx = 3.0 + (i % 4);
            in.mouse_dy = twin.mouse_dy = (i % 3) - 1.0;
            const double seconds = in.delta_time / 4096.0;
            const cam::AltLook ahead = c.look_ahead(in, seconds);
            const cam::AltCameraOutput out = step(c, in, level);
            const cam::AltCameraOutput plain = step(untouched, twin, level);
            SH_CHECK(std::fabs(std::remainder(ahead.yaw - out.basis.yaw, 4096.0)) < 1e-6);
            SH_CHECK(std::fabs(ahead.pitch - out.basis.pitch) < 1e-6);
            SH_CHECK(out.yaw == plain.yaw && out.pitch == plain.pitch);
            SH_CHECK(out.eye == plain.eye && out.look_at == plain.look_at);
        }
    }
}

// A held stick turns the view evenly over the frames between two updates: the angle a quarter, a half and three
// quarters of a step ahead lies on the straight line to the next update's angle. Mouse counts are shown at once,
// whatever the time.
SH_TEST(camera_alt_look_ahead_is_an_even_ramp) {
    for (const cam::AltStyle style : {cam::AltStyle::FirstPerson, cam::AltStyle::ThirdPerson}) {
        cam::AltCamera c(shipped());
        Open level;
        cam::AltCameraInput in = input(style);
        in.right_x = 128 + 100;
        for (int i = 0; i < 5; ++i) step(c, in, level);
        const double whole = in.delta_time / 4096.0;
        const double from = c.look_ahead(in, 0.0).yaw;
        const double to = c.look_ahead(in, whole).yaw;
        SH_CHECK(to - from > 1.0);
        for (int q = 1; q < 4; ++q) {
            const double at = c.look_ahead(in, whole * q / 4.0).yaw;
            SH_CHECK(std::fabs(at - (from + (to - from) * q / 4.0)) < 1e-9);
        }
        in.right_x = 128;
        in.mouse_dx = 7.0;
        const double mouse = c.look_ahead(in, 0.0).yaw;
        SH_CHECK(std::fabs(mouse - from - 7.0 * 6.0) < 1e-9);  // 6 units a count at sensitivity 1
        SH_CHECK(c.look_ahead(in, whole).yaw == mouse);
    }
}

// Look smoothing eases the turn, and still evenly: once a held stick's ease has settled (two seconds), the view
// turns by the stick's own rate on every update, and the quarters of a step between two updates lie on the same
// straight line - it does not surge after each update and wait before the next.
SH_TEST(camera_alt_look_ahead_is_even_with_smoothing) {
    cam::AltCamera c(shipped());
    Open level;
    cam::AltCameraInput in = input(cam::AltStyle::ThirdPerson);
    in.options.look_smoothing = 1.0f;
    in.right_x = 255;
    cam::AltCameraOutput out;
    for (int i = 0; i < 120; ++i) out = step(c, in, level);
    const double before = out.basis.yaw;
    out = step(c, in, level);
    // Full deflection is 40 units per 30 fps frame: 1200 a second, 68/4096 of it a step.
    const double per_step = 1200.0 * kFrame60 / 4096.0;
    SH_CHECK(std::fabs(std::remainder(out.basis.yaw - before, 4096.0) - per_step) < 1e-3);
    const double whole = in.delta_time / 4096.0;
    const double from = c.look_ahead(in, 0.0).yaw;
    for (int q = 1; q <= 4; ++q) {
        SH_CHECK(std::fabs(c.look_ahead(in, whole * q / 4.0).yaw - from - per_step * q / 4.0) < 1e-3);
    }
}

// The frames between two updates keep the camera's limits: the pitch stops where the update stops it, and in first
// person the look stops at the shoulders (90 degrees, 1024 units, off the body).
SH_TEST(camera_alt_look_ahead_keeps_the_limits) {
    const cam::AltCameraTuning t = shipped();
    Open level;
    {
        cam::AltCamera c(t);
        cam::AltCameraInput in = input(cam::AltStyle::ThirdPerson);
        step(c, in, level);
        in.mouse_dy = -100000.0;
        const double up = c.look_ahead(in, 0.0).pitch;
        const cam::AltCameraOutput out = step(c, in, level);
        SH_CHECK(up == out.pitch);
        in.mouse_dy = 100000.0;
        const double down = c.look_ahead(in, 0.0).pitch;
        SH_CHECK(down == step(c, in, level).pitch);
        SH_CHECK(up > down);
    }
    {
        cam::AltCamera c(t);
        cam::AltCameraInput in = input(cam::AltStyle::FirstPerson);
        in.harry_yaw = 500;
        in.yaw = 500;
        step(c, in, level);
        in.mouse_dx = 100000.0;
        SH_CHECK(c.look_ahead(in, 0.0).yaw == 500.0 + 1024.0);
        in.mouse_dx = -100000.0;
        SH_CHECK(c.look_ahead(in, 0.0).yaw == 500.0 - 1024.0);
        in.mouse_dx = 0.0;
        in.mouse_dy = -100000.0;
        const double up = c.look_ahead(in, 0.0).pitch;
        SH_CHECK(up == step(c, in, level).pitch);
    }
}

// The console and the quick options hold the view: neither an update nor a frame between two turns it.
SH_TEST(camera_alt_look_frozen_holds_still) {
    for (const cam::AltStyle style : {cam::AltStyle::FirstPerson, cam::AltStyle::ThirdPerson}) {
        cam::AltCamera c(shipped());
        Open level;
        cam::AltCameraInput in = input(style);
        in.yaw = 100;
        step(c, in, level);
        in.frozen = true;
        in.mouse_dx = 50.0;
        in.right_x = 255;
        SH_CHECK(c.look_ahead(in, 0.01).yaw == 100.0);
        SH_CHECK(step(c, in, level).yaw == 100);
    }
}
