// The PC alternate cameras (engine/camera/alt_camera.h), with the
// shipped tuning (gamedata/config/alt_cameras.json): the third person spring arm (orbit, wall cone,
// shoulder at a wall, look smoothing, stick curve, frame rate independence, lag limit, teleport,
// recenter, aim zoom), first person and the field of view.

#include "engine/camera/alt_camera.h"

#include "framework/sh_test.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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

class Walls : public cam::AltCameraHost {
public:
    std::int32_t hit = -1;  // distance of a hit on every ray, -1 = open space
    int rays = 0;
    bool ray_hit(const cam::AltVec3&, const cam::AltVec3&, std::int32_t* distance) override {
        ++rays;
        if (hit < 0) return false;
        *distance = hit;
        return true;
    }
};

// Walls as planes x = v or z = v: a ray that crosses one hits it, at the crossing.
class Planes : public cam::AltCameraHost {
public:
    struct Plane {
        int axis;  // 0 = x, 2 = z
        std::int32_t at;
    };
    std::vector<Plane> planes;
    int rays = 0;
    bool ray_hit(const cam::AltVec3& from, const cam::AltVec3& to, std::int32_t* distance) override {
        ++rays;
        const double a[3] = {double(from.x), double(from.y), double(from.z)};
        const double b[3] = {double(to.x), double(to.y), double(to.z)};
        const double len = std::sqrt((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) +
                                     (b[2] - a[2]) * (b[2] - a[2]));
        double best = -1.0;
        for (const Plane& p : planes) {
            const double da = a[p.axis] - p.at;
            const double db = b[p.axis] - p.at;
            if ((da < 0.0) == (db < 0.0)) continue;
            const double t = da / (da - db);
            if (best < 0.0 || t * len < best) best = t * len;
        }
        if (best < 0.0) return false;
        *distance = static_cast<std::int32_t>(best);
        return true;
    }
};

constexpr std::int32_t kFrame60 = 4096 / 60;  // Q12 seconds

cam::AltCameraInput rigid() {
    cam::AltCameraInput in;
    in.options.follow_smoothing = 0.0f;
    in.options.recenter = false;
    return in;
}

}  // namespace

SH_TEST(camera_alt_third_person_orbit) {
    cam::AltCamera c(shipped());
    Walls walls;
    cam::AltCameraInput in = rigid();
    const cam::AltCameraOutput out = c.update(in, walls);
    // Yaw 0 looks along +Z: the eye 2.5 behind, 1.4 up; the look target 25 ahead at chest height.
    SH_CHECK(out.eye == (cam::AltVec3{0, -5734, -10240}));
    SH_CHECK(out.look_at == (cam::AltVec3{0, -3481, -10240 + 102400}));
    SH_CHECK(out.aim_origin == out.eye);
    SH_CHECK(out.aim_forward.x == 0 && out.aim_forward.z > 4000 && out.aim_forward.y > 0);
    SH_CHECK(walls.rays == 5);  // the cone: the eye and four points around it
    SH_CHECK(!out.first_person);
}

SH_TEST(camera_alt_wall_pull_in_and_ease_out) {
    cam::AltCamera c(shipped());
    Planes level;
    level.planes.push_back({2, -4096});  // a wall one unit behind Harry
    cam::AltCameraInput in = rigid();
    in.delta_time = kFrame60;
    cam::AltCameraOutput out = c.update(in, level);
    // In front of the wall by about the 0.25 margin, at once.
    SH_CHECK(out.eye.z > -4096 + 900 && out.eye.z < -4096 + 1100);
    // ... and lifted above the head (1.55 up), so the head does not fill the view.
    SH_CHECK(out.eye.y < -6348 - 150);
    const std::int32_t pulled = out.eye.z;
    // The wall goes: the eye eases back out, not in one frame.
    level.planes.clear();
    out = c.update(in, level);
    SH_CHECK(out.eye.z < pulled && out.eye.z > -10240 + 1000);
    for (int i = 0; i < 120; i++) out = c.update(in, level);
    SH_CHECK(out.eye.z <= -10240 + 4 && out.eye.z >= -10240);
    // It comes back: in again at once.
    level.planes.push_back({2, -4096});
    out = c.update(in, level);
    SH_CHECK(out.eye.z > -4096);
    // With wall collision off the eye stays, the aim ray starts in front of the wall.
    cam::AltCamera c2(shipped());
    in.options.wall_collision = false;
    out = c2.update(in, level);
    SH_CHECK(out.eye.z == -10240);
    SH_CHECK(out.aim_origin.z > -4096);
}

SH_TEST(camera_alt_shoulder_narrows_at_a_side_wall) {
    cam::AltCamera c(shipped());
    Planes level;
    level.planes.push_back({0, 1638});  // a wall 0.4 to Harry's right
    cam::AltCameraInput in = rigid();
    in.style = cam::AltStyle::ThirdPerson;
    in.options.rest_x = 3686;  // 0.9 to the right
    cam::AltCameraOutput out = c.update(in, level);
    // The shoulder point stays short of the wall; the arm itself keeps (most of) its length.
    SH_CHECK(out.eye.x > 0 && out.eye.x < 1638 - 600);
    SH_CHECK(out.eye.z < -8000);
    level.planes.clear();
    cam::AltCamera open(shipped());
    out = open.update(in, level);
    SH_CHECK(out.eye.x == 3686);
}

SH_TEST(camera_alt_look_and_limits) {
    cam::AltCamera c(shipped());
    Walls walls;
    cam::AltCameraInput in = rigid();
    in.mouse_dx = 100;  // 6 units a pixel
    in.mouse_dy = 1000; // down: pitch up, limited to -40 degrees
    cam::AltCameraOutput out = c.update(in, walls);
    SH_CHECK(out.yaw == 600);
    SH_CHECK(out.pitch == -455);
    SH_CHECK(out.aim_device == 0);
    // Frozen: nothing turns, but a held stick still marks the pad.
    in.yaw = out.yaw;
    in.pitch = out.pitch;
    in.frozen = true;
    in.left_x = 255;
    out = c.update(in, walls);
    SH_CHECK(out.yaw == 600 && out.pitch == -455);
    SH_CHECK(out.aim_device == 1);
    // First person: at most 90 degrees left or right of the body.
    cam::AltCamera f(shipped());
    in = {};
    in.style = cam::AltStyle::FirstPerson;
    in.harry_yaw = 1000;
    in.mouse_dx = 400;
    in.options.head_tracking = false;
    out = f.update(in, walls);
    SH_CHECK(out.yaw == 1000 + 1024);
    SH_CHECK(out.first_person);
}

SH_TEST(camera_alt_look_smoothing_keeps_the_whole_turn) {
    cam::AltCamera c(shipped());
    Walls walls;
    cam::AltCameraInput in = rigid();
    in.options.look_smoothing = 1.0f;
    in.delta_time = kFrame60;
    in.mouse_dx = 100;
    cam::AltCameraOutput out = c.update(in, walls);
    SH_CHECK(out.yaw > 0 && out.yaw < 600);
    in.mouse_dx = 0;
    for (int i = 0; i < 120; i++) {
        in.yaw = out.yaw;
        out = c.update(in, walls);
    }
    SH_CHECK(out.yaw == 600);
}

SH_TEST(camera_alt_stick_curve) {
    Walls walls;
    const auto turn = [&](int right_x) {
        cam::AltCamera c(shipped());
        cam::AltCameraInput in = rigid();
        in.delta_time = 4096 / 30;
        in.right_x = right_x;
        return c.update(in, walls).yaw;
    };
    const std::int32_t full = turn(255);
    const std::int32_t half = turn(128 + 64);
    SH_CHECK(full >= 38 && full <= 40);  // the tuned 40 a 30 fps frame
    SH_CHECK(half > 0 && half * 10 < full * 4);
    SH_CHECK(turn(128 + 20) == 0);  // inside the dead zone
}

// Harry runs along +Z at 4 units a second for a second; the camera ends in the same place whatever
// the frame rate (32 and 128 fps: frame times exact in Q12 seconds).
static cam::AltVec3 run_for_a_second(int fps, bool aiming) {
    cam::AltCamera c(shipped());
    Walls walls;
    cam::AltCameraInput in;
    in.options.recenter = false;
    in.options.follow_smoothing = 1.0f;
    in.delta_time = 0;
    c.update(in, walls);
    in.aiming = aiming;
    cam::AltCameraOutput out;
    for (int i = 1; i <= fps; i++) {
        in.delta_time = 4096 / fps;
        in.harry.z = static_cast<std::int32_t>(4.0 * 4096.0 * i / fps);
        out = c.update(in, walls);
    }
    return out.eye;
}

SH_TEST(camera_alt_frame_rate_independent) {
    const cam::AltVec3 a = run_for_a_second(32, false);
    const cam::AltVec3 b = run_for_a_second(128, false);
    // The camera trails Harry (16384 at the end) while he runs ...
    SH_CHECK(a.z < 16384 - 10240 - 200);
    // ... by the same amount at 32 and 128 fps.
    SH_CHECK(std::abs(a.z - b.z) < 8);
    SH_CHECK(std::abs(a.y - b.y) < 8);
    // Aiming: the zoom and the tighter follow land in the same place too.
    const cam::AltVec3 c = run_for_a_second(32, true);
    const cam::AltVec3 d = run_for_a_second(128, true);
    SH_CHECK(c.z > a.z);
    SH_CHECK(std::abs(c.z - d.z) < 8);
}

SH_TEST(camera_alt_follow_lag_limit_and_teleport) {
    cam::AltCamera c(shipped());
    Walls walls;
    cam::AltCameraInput in;
    in.options.recenter = false;
    in.options.follow_smoothing = 1.0f;
    c.update(in, walls);
    in.delta_time = kFrame60;
    cam::AltCameraOutput out;
    for (int i = 1; i <= 30; i++) {
        in.harry.z = i * 1300;  // far faster than Harry runs
        out = c.update(in, walls);
    }
    // Never more than 0.6 behind.
    SH_CHECK(out.eye.z >= in.harry.z - 10240 - 2458 - 2);
    // A jump (a door, a warp) snaps.
    in.harry.z += 40960;
    out = c.update(in, walls);
    SH_CHECK(out.eye.z == in.harry.z - 10240);
}

SH_TEST(camera_alt_recenter) {
    Walls walls;
    const auto settle = [&](bool moving) {
        cam::AltCamera c(shipped());
        cam::AltCameraInput in;
        in.options.follow_smoothing = 0.0f;
        in.options.recenter_delay = 0.5f;
        in.delta_time = kFrame60;
        in.moving = moving;
        in.pitch = 300;
        cam::AltCameraOutput out;
        for (int i = 0; i < 240; i++) {
            out = c.update(in, walls);
            in.pitch = out.pitch;
        }
        return out.pitch;
    };
    SH_CHECK(std::abs(settle(true) - 45) <= 2);  // 4 degrees
    SH_CHECK(settle(false) == 300);
}

SH_TEST(camera_alt_aim_zoom) {
    cam::AltCamera c(shipped());
    Walls walls;
    cam::AltCameraInput in = rigid();
    c.update(in, walls);
    in.aiming = true;
    in.delta_time = kFrame60;
    // 100%: eases toward 1.3 units (5324): part of the way in a frame, all of it in a second.
    cam::AltCameraOutput out = c.update(in, walls);
    SH_CHECK(out.eye.z < -5340 && out.eye.z > -10240);
    for (int i = 0; i < 60; i++) {
        out = c.update(in, walls);
    }
    SH_CHECK(out.eye.z > -5340 && out.eye.z < -5310);
}

SH_TEST(camera_alt_first_person_eye) {
    cam::AltCamera c(shipped());
    Walls walls;
    cam::AltCameraInput in;
    in.style = cam::AltStyle::FirstPerson;
    in.options.head_tracking = false;
    in.options.eye_offset = cam::AltVec3{-29, -6836, 919};
    in.harry = cam::AltVec3{4096, 0, 8192};
    // Head bone exactly where the reference will be: the eye is the tuned offset.
    in.head.ty = -6836 >> 4;
    const cam::AltCameraOutput out = c.update(in, walls);
    SH_CHECK(out.eye == (cam::AltVec3{4096 - 29, -6836, 8192 + 919}));
    SH_CHECK(out.view_forward == (cam::AltVec3{0, 0, 4096}));
    SH_CHECK(walls.rays == 0);  // no swing, no wall pull in first person
}

SH_TEST(camera_alt_first_person_returns_to_the_eyes_after_a_scene) {
    cam::AltCamera c(shipped());
    Walls walls;
    cam::AltCameraInput in;
    in.style = cam::AltStyle::FirstPerson;
    in.options.head_tracking = false;
    in.options.eye_offset = cam::AltVec3{0, -6836, 919};
    in.in_gameplay = true;
    in.delta_time = kFrame60;
    in.head.ty = -6836 >> 4;
    in.yaw = 300;
    in.pitch = 200;
    c.update(in, walls);  // the head reference is taken here

    // A scene: Harry ends it elsewhere, turned around, in another pose (the head 0.5 m lower and forward).
    in.harry = cam::AltVec3{40960, 0, -8192};
    in.harry_yaw = 2048;
    in.head.tx = 40960 >> 4;
    in.head.ty = (-6836 + 2048) >> 4;
    in.head.tz = (-8192 - 2048) >> 4;
    in.reset = true;
    in.in_gameplay = false;  // the first frame back is not settled gameplay yet
    cam::AltCameraOutput out = c.update(in, walls);
    // At once in his eyes, level, looking where he faces: nothing of the old view or head reference is left.
    SH_CHECK(out.yaw == 2048 && out.pitch == 0);
    SH_CHECK(out.eye == (cam::AltVec3{40960, -6836, -8192 - 919}));
    SH_CHECK(out.view_forward.x == 0 && out.view_forward.y == 0 && out.view_forward.z == -4096);

    // The next frames hold it: the eye does not travel.
    in.reset = false;
    in.in_gameplay = true;
    in.yaw = out.yaw;
    in.pitch = out.pitch;
    for (int i = 0; i < 30; ++i) {
        out = c.update(in, walls);
        SH_CHECK(out.eye == (cam::AltVec3{40960, -6836, -8192 - 919}));
        SH_CHECK(out.yaw == 2048);
    }

    // A pose change in play (he stands up 0.5 m): the eye follows by the sway limit only, and stays there.
    const std::int32_t limit = static_cast<std::int32_t>(shipped().head_sway_limit * 4096.0f);
    SH_CHECK(limit > 0 && limit < 1024);
    in.head.ty = -6836 >> 4;
    out = c.update(in, walls);
    SH_CHECK(std::abs(out.eye.y - (-6836 - limit)) <= 16);
    SH_CHECK(out.eye.x == 40960);
}

SH_TEST(camera_alt_third_person_returns_behind_harry_after_a_scene) {
    const cam::AltCameraTuning t = shipped();
    cam::AltCamera c(t);
    Walls walls;
    cam::AltCameraInput in;
    in.delta_time = kFrame60;
    in.yaw = 700;
    in.pitch = 150;
    for (int i = 0; i < 10; ++i) c.update(in, walls);

    // The scene ends two metres away with Harry facing +X; the view is behind him on the same frame.
    in.harry = cam::AltVec3{8192, 0, 4096};
    in.harry_yaw = 1024;
    in.reset = true;
    const cam::AltCameraOutput out = c.update(in, walls);
    SH_CHECK(out.yaw == 1024 && out.pitch == 0);
    const std::int32_t arm = static_cast<std::int32_t>(t.orbit_distance * 4096.0f);
    SH_CHECK(std::abs(out.eye.x - (8192 - arm)) <= 2 && std::abs(out.eye.z - 4096) <= 2);
}

// A corridor 1 m wide: the eye never leaves it, comes in on the same frame and does not pump.
SH_TEST(camera_alt_corridor_keeps_the_eye_inside) {
    const cam::AltCameraTuning t = shipped();
    cam::AltCamera c(t);
    Planes level;
    level.planes = {{0, -2048}, {0, 2048}};
    cam::AltCameraInput in;
    in.delta_time = kFrame60;
    in.harry_yaw = 0;
    in.yaw = 0;
    cam::AltCameraOutput out = c.update(in, level);
    std::int32_t last_x = out.eye.x;
    // The view swings from along the corridor to square across it and back.
    for (int i = 0; i < 240; ++i) {
        in.yaw = i < 120 ? (i * 1024) / 120 : ((240 - i) * 1024) / 120;
        in.pitch = out.pitch;
        out = c.update(in, level);
        SH_CHECK(out.eye.x > -2048 && out.eye.x < 2048);
        // No frame-to-frame jump larger than the swing itself can make (the full arm is 2.5 m).
        SH_CHECK(std::abs(out.eye.x - last_x) < 1024);
        last_x = out.eye.x;
    }
}

SH_TEST(camera_alt_field_of_view) {
    SH_CHECK(cam::AltCamera::projection_distance(71.1f, 0) == 224);
    SH_CHECK(cam::AltCamera::projection_distance(179.9f, 0) == 16);
    SH_CHECK(cam::AltCamera::projection_distance(0.0f, 240) == 240);
}
