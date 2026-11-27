// A frame's scene seen from another camera (engine/app/loop/scene_aim.h): the camera built from an eye and a look
// target, and the frame's view-space content carried over to it.

#include "engine/app/loop/scene_aim.h"

#include "framework/sh_test.h"

#include <cmath>

namespace {

namespace scene = sh::scene;

bool near(double a, double b, double tolerance = 1e-4) { return std::fabs(a - b) <= tolerance; }

// Where a camera sees a world point (scene units).
std::array<double, 3> seen(const scene::Camera& c, double x, double y, double z) {
    return {c.rot[0] * x + c.rot[1] * y + c.rot[2] * z + c.trans[0],
            c.rot[3] * x + c.rot[4] * y + c.rot[5] * z + c.trans[1],
            c.rot[6] * x + c.rot[7] * y + c.rot[8] * z + c.trans[2]};
}

}  // namespace

// Looking along +Z from a point: the axes are the world's (x right, y down, z ahead) and the eye is the origin
// of the view; positions come in Q12 units, 16 to a scene unit.
SH_TEST(scene_aim_camera_looking_level) {
    const double eye[3] = {1600.0, -3200.0, 4800.0};
    const double at[3] = {1600.0, -3200.0, 4800.0 + 40960.0};
    scene::Camera c;
    SH_CHECK(sh::loop::camera_looking(eye, at, &c));
    const std::array<double, 9> identity = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (std::size_t i = 0; i < 9; ++i) SH_CHECK(near(c.rot[i], identity[i]));
    SH_CHECK(near(c.trans[0], -100.0) && near(c.trans[1], 200.0) && near(c.trans[2], -300.0));
    // A point ahead and to the right of the eye, above it: +x, -y, +z in view.
    const auto p = seen(c, 100.0 + 10.0, -200.0 - 5.0, 300.0 + 50.0);
    SH_CHECK(near(p[0], 10.0) && near(p[1], -5.0) && near(p[2], 50.0));
}

// Turned a quarter to the right (+X ahead) and looking down: the target is in the middle of the view, the horizon
// stays level, and a camera looking straight down has no level and is refused.
SH_TEST(scene_aim_camera_looking_turned) {
    const double eye[3] = {0.0, 0.0, 0.0};
    const double at[3] = {16000.0, 16000.0, 0.0};
    scene::Camera c;
    SH_CHECK(sh::loop::camera_looking(eye, at, &c));
    const auto p = seen(c, 1000.0, 1000.0, 0.0);
    SH_CHECK(near(p[0], 0.0) && near(p[1], 0.0) && near(p[2], std::sqrt(2.0) * 1000.0, 1e-3));
    // The right axis is level, and +Z of the world is to the left of a view along +X.
    SH_CHECK(near(c.rot[1], 0.0) && near(c.rot[0], 0.0) && near(c.rot[2], -1.0));
    const double down[3] = {0.0, 4096.0, 0.0};
    SH_CHECK(!sh::loop::camera_looking(eye, down, &c));
    SH_CHECK(!sh::loop::camera_looking(eye, eye, &c));
}

SH_TEST(scene_aim_camera_gap) {
    const double eye[3] = {0.0, 0.0, 0.0};
    const double eye2[3] = {160.0, 0.0, 0.0};
    const double at[3] = {0.0, 0.0, 4096.0};
    const double at2[3] = {160.0, 0.0, 4096.0};
    scene::Camera a, b;
    SH_CHECK(sh::loop::camera_looking(eye, at, &a));
    SH_CHECK(sh::loop::camera_looking(eye2, at2, &b));
    const sh::loop::CameraGap same = sh::loop::camera_gap(a, a);
    SH_CHECK(same.turn == 0.0 && same.move == 0.0);
    const sh::loop::CameraGap moved = sh::loop::camera_gap(a, b);
    SH_CHECK(near(moved.turn, 0.0) && near(moved.move, 10.0));
}

// The game's camera is rounded: its rotation to 1/4096 and its translation to whole units. 160 m from the world's
// origin such a camera is still within a unit or two of the exact one it was rounded from, and a vertex it drew
// is carried to another camera as the world point it was drawn from, not centimetres beside it.
SH_TEST(scene_aim_takes_the_rounded_camera_as_it_draws) {
    const double eye[3] = {-25334.0, -5734.0, 667644.0};
    const double at[3] = {-25334.0 + 61000.0, -5734.0 + 9000.0, 667644.0 + 82000.0};
    scene::Camera exact;
    SH_CHECK(sh::loop::camera_looking(eye, at, &exact));
    scene::Camera rounded;
    for (std::size_t i = 0; i < 9; ++i) rounded.rot[i] = std::round(exact.rot[i] * 4096.0) / 4096.0;
    const double e[3] = {std::floor(eye[0] / 16.0), std::floor(eye[1] / 16.0), std::floor(eye[2] / 16.0)};
    for (std::size_t r = 0; r < 3; ++r)
        rounded.trans[r] =
            std::round(-(rounded.rot[r * 3] * e[0] + rounded.rot[r * 3 + 1] * e[1] + rounded.rot[r * 3 + 2] * e[2]));
    const sh::loop::CameraGap gap = sh::loop::camera_gap(rounded, exact);
    SH_CHECK(gap.turn < 0.0002 && gap.move < 2.5);
    // It stands where it draws from, within its rounding.
    const std::array<double, 3> stands = sh::loop::camera_eye(rounded);
    SH_CHECK(near(stands[0], e[0], 2.5) && near(stands[1], e[1], 2.5) && near(stands[2], e[2], 2.5));

    const double world[3] = {e[0] + 300.0, e[1] + 40.0, e[2] + 500.0};
    const auto drawn = seen(rounded, world[0], world[1], world[2]);
    scene::Frame frame;
    frame.world = true;
    frame.camera = rounded;
    scene::PrelitVertex v;
    v.x = static_cast<float>(drawn[0]);
    v.y = static_cast<float>(drawn[1]);
    v.z = static_cast<float>(drawn[2]);
    scene::PrelitMesh mesh;
    mesh.vertices = {v};
    frame.prelit = {mesh};
    scene::Beam beam;
    sh::loop::aim_scene(frame, beam, exact);
    const auto want = seen(exact, world[0], world[1], world[2]);
    const scene::PrelitVertex& moved = frame.prelit[0].vertices[0];
    SH_CHECK(near(moved.x, want[0], 0.05) && near(moved.y, want[1], 0.05) && near(moved.z, want[2], 0.05));
}

// What the frame holds in view space stays where it is in the world when the camera changes: a posed mesh's
// vertex, a flake and the flashlight (a point and a direction) are seen from the new camera as the same world
// point and direction; an isolated model (its own projection) is left alone.
SH_TEST(scene_aim_carries_the_view_space_content) {
    const double eye_a[3] = {1600.0, -1600.0, 0.0};
    const double at_a[3] = {1600.0 + 30000.0, -1600.0 + 3000.0, 20000.0};
    const double eye_b[3] = {1700.0, -1650.0, 90.0};
    const double at_b[3] = {1700.0 + 25000.0, -1650.0 - 2000.0, 90.0 + 27000.0};
    scene::Camera a, b;
    SH_CHECK(sh::loop::camera_looking(eye_a, at_a, &a));
    SH_CHECK(sh::loop::camera_looking(eye_b, at_b, &b));

    const double world[3] = {700.0, -80.0, 450.0};
    const auto in_a = seen(a, world[0], world[1], world[2]);
    const auto in_b = seen(b, world[0], world[1], world[2]);
    // A world direction, in each view.
    const double dir[3] = {0.6, 0.0, 0.8};
    const auto dir_in = [&](const scene::Camera& c) {
        return std::array<double, 3>{c.rot[0] * dir[0] + c.rot[1] * dir[1] + c.rot[2] * dir[2],
                                     c.rot[3] * dir[0] + c.rot[4] * dir[1] + c.rot[5] * dir[2],
                                     c.rot[6] * dir[0] + c.rot[7] * dir[1] + c.rot[8] * dir[2]};
    };
    const auto dir_a = dir_in(a), dir_b = dir_in(b);

    scene::Frame frame;
    frame.world = true;
    frame.camera = a;
    scene::PrelitVertex v;
    v.x = static_cast<float>(in_a[0]);
    v.y = static_cast<float>(in_a[1]);
    v.z = static_cast<float>(in_a[2]);
    scene::PrelitMesh mesh;
    mesh.vertices = {v};
    mesh.patch_normals = {
        {static_cast<float>(dir_a[0]), static_cast<float>(dir_a[1]), static_cast<float>(dir_a[2]), 3.0f}};
    scene::PrelitMesh item = mesh;
    item.isolated = true;
    frame.prelit = {mesh, item};
    frame.flakes = {{v.x, v.y, v.z, 2.0f}};
    scene::Beam beam;
    beam.on = true;
    beam.position = {v.x, v.y, v.z};
    beam.shadow_position = {v.x, v.y, v.z};
    beam.direction = {static_cast<float>(dir_a[0]), static_cast<float>(dir_a[1]), static_cast<float>(dir_a[2])};

    sh::loop::aim_scene(frame, beam, b);

    const double tolerance = 1e-3;
    const scene::PrelitVertex& moved = frame.prelit[0].vertices[0];
    SH_CHECK(near(moved.x, in_b[0], tolerance) && near(moved.y, in_b[1], tolerance) &&
             near(moved.z, in_b[2], tolerance));
    const auto& normal = frame.prelit[0].patch_normals[0];
    SH_CHECK(near(normal[0], dir_b[0], tolerance) && near(normal[1], dir_b[1], tolerance) &&
             near(normal[2], dir_b[2], tolerance) && normal[3] == 3.0f);
    SH_CHECK(frame.prelit[1].vertices[0].x == v.x && frame.prelit[1].vertices[0].z == v.z);
    SH_CHECK(near(frame.flakes[0][0], in_b[0], tolerance) && near(frame.flakes[0][2], in_b[2], tolerance) &&
             frame.flakes[0][3] == 2.0f);
    SH_CHECK(near(beam.position[0], in_b[0], tolerance) && near(beam.shadow_position[1], in_b[1], tolerance));
    SH_CHECK(near(beam.direction[0], dir_b[0], tolerance) && near(beam.direction[1], dir_b[1], tolerance) &&
             near(beam.direction[2], dir_b[2], tolerance));
    SH_CHECK(frame.camera.rot == b.rot && frame.camera.trans == b.trans);

    // Aimed at the camera it already has, the frame does not change.
    const scene::PrelitVertex before = frame.prelit[0].vertices[0];
    sh::loop::aim_scene(frame, beam, b);
    SH_CHECK(near(frame.prelit[0].vertices[0].x, before.x, 1e-4) &&
             near(frame.prelit[0].vertices[0].z, before.z, 1e-4));
}
