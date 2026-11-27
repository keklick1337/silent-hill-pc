// The scene of a frame between two game updates (render/scene/frame_blend.h): the
// camera, the posed meshes, the world objects and the flashlight move along the straight line between the two
// updates; a cut, a new mesh or a jump is shown as the newer update has it.

#include "framework/sh_test.h"
#include "render/scene/area_cells.h"
#include "render/scene/frame_blend.h"

#include <cmath>

namespace sc = sh::scene;

namespace {

bool near(double a, double b, double eps = 1e-3) { return std::fabs(a - b) < eps; }

// A camera at `eye` turned by `yaw` radians about the vertical axis.
sc::Camera camera_at(double x, double y, double z, double yaw) {
    sc::Camera c;
    const double s = std::sin(yaw), k = std::cos(yaw);
    c.rot = {k, 0, s, 0, 1, 0, -s, 0, k};
    for (std::size_t r = 0; r < 3; ++r) c.trans[r] = -(c.rot[r * 3] * x + c.rot[r * 3 + 1] * y + c.rot[r * 3 + 2] * z);
    return c;
}

sc::PrelitVertex view_of(const sc::Camera& c, double x, double y, double z) {
    sc::PrelitVertex v;
    v.x = static_cast<float>(c.rot[0] * x + c.rot[1] * y + c.rot[2] * z + c.trans[0]);
    v.y = static_cast<float>(c.rot[3] * x + c.rot[4] * y + c.rot[5] * z + c.trans[1]);
    v.z = static_cast<float>(c.rot[6] * x + c.rot[7] * y + c.rot[8] * z + c.trans[2]);
    return v;
}

// Where a view-space vertex of `c` stands in the world.
std::array<double, 3> world_of(const sc::Camera& c, const sc::PrelitVertex& v) {
    const double p[3] = {v.x - c.trans[0], v.y - c.trans[1], v.z - c.trans[2]};
    return {c.rot[0] * p[0] + c.rot[3] * p[1] + c.rot[6] * p[2], c.rot[1] * p[0] + c.rot[4] * p[1] + c.rot[7] * p[2],
            c.rot[2] * p[0] + c.rot[5] * p[1] + c.rot[8] * p[2]};
}

// A frame with one triangle whose first corner stands at (x, 0, 1000) in the world.
sc::Frame frame_with_mesh(const sc::Camera& c, double x) {
    sc::Frame f;
    f.world = true;
    f.camera = c;
    sc::PrelitMesh m;
    m.texture_key = 7;
    m.vertices = {view_of(c, x, 0, 1000), view_of(c, x + 10, 0, 1000), view_of(c, x, 10, 1000)};
    m.indices = {0, 1, 2};
    f.prelit.push_back(m);
    return f;
}

}  // namespace

SH_TEST(scene_blend_camera_and_mesh_move_on_a_line) {
    const sc::Frame a = frame_with_mesh(camera_at(0, 0, 0, 0.0), 100);
    const sc::Frame b = frame_with_mesh(camera_at(40, 0, 0, 0.2), 140);
    sc::Frame out;
    out.serial = 5;
    sc::blend_frames(a, b, 0.25, out);
    SH_CHECK(out.serial == 5);
    // The eye a quarter of the way, the turn a quarter of the angle.
    const sc::Camera want = camera_at(10, 0, 0, 0.05);
    for (std::size_t i = 0; i < 9; ++i) SH_CHECK(near(out.camera.rot[i], want.rot[i], 1e-4));
    for (std::size_t i = 0; i < 3; ++i) SH_CHECK(near(out.camera.trans[i], want.trans[i], 0.05));
    // The mesh a quarter of the way in the world, whatever the cameras did.
    const std::array<double, 3> p = world_of(out.camera, out.prelit[0].vertices[0]);
    SH_CHECK(near(p[0], 110, 0.05) && near(p[1], 0, 0.05) && near(p[2], 1000, 0.05));

    sc::blend_frames(a, b, 0.0, out);
    SH_CHECK(near(world_of(out.camera, out.prelit[0].vertices[0])[0], 100, 0.05));
    for (std::size_t i = 0; i < 3; ++i) SH_CHECK(near(out.camera.trans[i], a.camera.trans[i], 0.05));
    sc::blend_frames(a, b, 1.0, out);
    for (std::size_t i = 0; i < 3; ++i) SH_CHECK(near(out.prelit[0].vertices[0].x, b.prelit[0].vertices[0].x, 0.05));
    // Past the ends nothing overshoots.
    sc::blend_frames(a, b, 3.0, out);
    SH_CHECK(near(world_of(out.camera, out.prelit[0].vertices[0])[0], 140, 0.05));
}

SH_TEST(scene_blend_cut_shows_the_newer_update) {
    const sc::Frame a = frame_with_mesh(camera_at(0, 0, 0, 0.0), 100);
    // The camera jumped across the room: no frame shows a place in between.
    const sc::Frame far = frame_with_mesh(camera_at(5000, 0, 0, 0.0), 140);
    sc::Frame out;
    sc::blend_frames(a, far, 0.5, out);
    SH_CHECK(out.camera.trans == far.camera.trans);
    SH_CHECK(out.prelit[0].vertices[0].x == far.prelit[0].vertices[0].x);
    // So did a turn of a right angle.
    const sc::Frame turned = frame_with_mesh(camera_at(0, 0, 0, 1.57), 140);
    sc::blend_frames(a, turned, 0.5, out);
    SH_CHECK(out.camera.rot == turned.camera.rot);
    // A frame without the world blends nothing.
    sc::Frame menu = a;
    menu.world = false;
    sc::blend_frames(menu, turned, 0.5, out);
    SH_CHECK(out.camera.rot == turned.camera.rot);
}

SH_TEST(scene_blend_unmatched_mesh_stands_where_the_newer_update_has_it) {
    const sc::Camera cam_a = camera_at(0, 0, 0, 0.0);
    const sc::Camera cam_b = camera_at(40, 0, 0, 0.0);
    const sc::Frame a = frame_with_mesh(cam_a, 100);
    // Another mesh took the place in the list (another texture), a new one was added, one teleported.
    sc::Frame b = frame_with_mesh(cam_b, 140);
    b.prelit[0].texture_key = 8;
    sc::Frame out;
    sc::blend_frames(a, b, 0.5, out);
    SH_CHECK(near(world_of(out.camera, out.prelit[0].vertices[0])[0], 140, 0.05));

    b = frame_with_mesh(cam_b, 140);
    b.prelit.push_back(b.prelit[0]);
    sc::blend_frames(a, b, 0.5, out);
    SH_CHECK(out.prelit.size() == 2);
    SH_CHECK(near(world_of(out.camera, out.prelit[0].vertices[0])[0], 120, 0.05));
    SH_CHECK(near(world_of(out.camera, out.prelit[1].vertices[0])[0], 140, 0.05));

    b = frame_with_mesh(cam_b, 900);
    sc::blend_frames(a, b, 0.5, out);
    SH_CHECK(near(world_of(out.camera, out.prelit[0].vertices[0])[0], 900, 0.05));

    // An isolated preview has its own camera: untouched.
    b = frame_with_mesh(cam_b, 140);
    b.prelit[0].isolated = true;
    sc::Frame iso = a;
    iso.prelit[0].isolated = true;
    sc::blend_frames(iso, b, 0.5, out);
    SH_CHECK(out.prelit[0].vertices[0].x == b.prelit[0].vertices[0].x);
}

SH_TEST(scene_blend_objects_and_beam) {
    sc::Frame a = frame_with_mesh(camera_at(0, 0, 0, 0.0), 100);
    sc::Frame b = frame_with_mesh(camera_at(0, 0, 0, 0.0), 100);
    const auto mesh = std::make_shared<const sc::CellMesh>();
    sc::ObjectRef held;
    held.mesh = mesh;
    held.trans = {0, 0, 0};
    a.objects.push_back(held);
    // A swung weapon: a third of a turn in one update still blends.
    const sc::Camera turn = camera_at(0, 0, 0, 1.0);
    held.rot = turn.rot;
    held.trans = {20, 0, 0};
    b.objects.push_back(held);
    sc::Frame out;
    sc::blend_frames(a, b, 0.5, out);
    const sc::Camera half = camera_at(0, 0, 0, 0.5);
    for (std::size_t i = 0; i < 9; ++i) SH_CHECK(near(out.objects[0].rot[i], half.rot[i], 1e-4));
    SH_CHECK(near(out.objects[0].trans[0], 10));
    // Another model in the slot: no blend.
    b.objects[0].mesh = std::make_shared<const sc::CellMesh>();
    sc::blend_frames(a, b, 0.5, out);
    SH_CHECK(near(out.objects[0].trans[0], 20));

    // The beam is given in each update's view space and moves through the world.
    const sc::Frame fa = frame_with_mesh(camera_at(0, 0, 0, 0.0), 100);
    const sc::Frame fb = frame_with_mesh(camera_at(40, 0, 0, 0.2), 100);
    sc::Beam from, to;
    from.on = to.on = true;
    const sc::PrelitVertex pa = view_of(fa.camera, 0, -300, 500), pb = view_of(fb.camera, 100, -300, 500);
    from.position = from.shadow_position = {pa.x, pa.y, pa.z};
    to.position = to.shadow_position = {pb.x, pb.y, pb.z};
    from.direction = to.direction = {0, 0, 1};
    sc::blend_frames(fa, fb, 0.5, out);
    const sc::Beam beam = sc::blend_beam(fa, from, fb, to, out, 0.5);
    sc::PrelitVertex bv;
    bv.x = beam.position[0];
    bv.y = beam.position[1];
    bv.z = beam.position[2];
    const std::array<double, 3> bw = world_of(out.camera, bv);
    SH_CHECK(near(bw[0], 50, 0.05) && near(bw[1], -300, 0.05) && near(bw[2], 500, 0.05));
    const double len = std::sqrt(beam.direction[0] * beam.direction[0] + beam.direction[1] * beam.direction[1] +
                                 beam.direction[2] * beam.direction[2]);
    SH_CHECK(near(len, 1.0, 1e-4));
    // Switched on this update: nothing to come from.
    from.on = false;
    SH_CHECK(sc::blend_beam(fa, from, fb, to, out, 0.5).position == to.position);
}

// A camera the game rounded (its rotation to 1/4096, its translation to whole units) 160 m from the world's origin:
// halfway between two such cameras that stand at the same place and turn a little, the blended camera still stands
// at that place (within the rounding, 2 units), and a vertex that stood still in the world is still seen there. The
// transpose of such a matrix would put the eye up to 20 units off, by another amount for each of the two.
SH_TEST(scene_blend_takes_rounded_cameras_as_they_draw) {
    const double eye[3] = {-1583.0, -358.0, 41727.0};
    const auto rounded = [&](double yaw) {
        sc::Camera c = camera_at(0, 0, 0, yaw);
        for (double& e : c.rot) e = std::round(e * 4096.0) / 4096.0;
        for (std::size_t r = 0; r < 3; ++r)
            c.trans[r] = std::round(-(c.rot[r * 3] * eye[0] + c.rot[r * 3 + 1] * eye[1] + c.rot[r * 3 + 2] * eye[2]));
        return c;
    };
    const double point[3] = {eye[0] + 100.0, eye[1] + 20.0, eye[2] + 600.0};
    const auto frame = [&](const sc::Camera& c) {
        sc::Frame f;
        f.world = true;
        f.camera = c;
        sc::PrelitMesh m;
        m.texture_key = 7;
        m.vertices = {view_of(c, point[0], point[1], point[2])};
        f.prelit.push_back(m);
        return f;
    };
    const sc::Frame a = frame(rounded(0.3001));
    const sc::Frame b = frame(rounded(0.3137));
    sc::Frame out;
    sc::blend_frames(a, b, 0.5, out);
    // Where the blended camera (a pure rotation) sees its own eye and the vertex.
    const std::array<double, 3> stands = world_of(out.camera, sc::PrelitVertex{});
    SH_CHECK(near(stands[0], eye[0], 2.0) && near(stands[1], eye[1], 2.0) && near(stands[2], eye[2], 2.0));
    const std::array<double, 3> seen = world_of(out.camera, out.prelit[0].vertices[0]);
    SH_CHECK(near(seen[0], point[0], 2.0) && near(seen[1], point[1], 2.0) && near(seen[2], point[2], 2.0));
}

SH_TEST(scene_tick_frames_keep_three_updates) {
    sc::TickFrames ticks;
    SH_CHECK(!ticks.has_last() && !ticks.has_pair());
    const sc::Frame a = frame_with_mesh(camera_at(0, 0, 0, 0.0), 100);
    const sc::Frame b = frame_with_mesh(camera_at(0, 0, 0, 0.0), 140);
    const sc::Frame c = frame_with_mesh(camera_at(0, 0, 0, 0.0), 180);
    sc::Beam beam;
    sc::Frame out;
    out.serial = 9;
    ticks.record(a, beam);
    SH_CHECK(ticks.has_last() && !ticks.has_pair());
    // One update alone is shown as it is.
    ticks.show(0.5, out, beam);
    SH_CHECK(out.serial == 9 && near(world_of(out.camera, out.prelit[0].vertices[0])[0], 100, 0.05));
    ticks.record(b, beam);
    SH_CHECK(ticks.has_pair());
    // The update just made is where the picture is heading: its frame starts from the one before.
    ticks.show(0.0, out, beam);
    SH_CHECK(near(world_of(out.camera, out.prelit[0].vertices[0])[0], 100, 0.05));
    ticks.show(0.5, out, beam);
    SH_CHECK(near(world_of(out.camera, out.prelit[0].vertices[0])[0], 120, 0.05));
    ticks.record(c, beam);
    ticks.show(0.5, out, beam);
    SH_CHECK(near(world_of(out.camera, out.prelit[0].vertices[0])[0], 160, 0.05));
    // Three are kept: a negative share goes back towards the update before the last two.
    SH_CHECK(ticks.has_three());
    ticks.show(-0.5, out, beam);
    SH_CHECK(near(world_of(out.camera, out.prelit[0].vertices[0])[0], 120, 0.05));
    ticks.show(-1.0, out, beam);
    SH_CHECK(near(world_of(out.camera, out.prelit[0].vertices[0])[0], 100, 0.05));
    // After a reset the next update has no predecessor.
    ticks.reset();
    SH_CHECK(!ticks.has_last());
    ticks.record(a, beam);
    SH_CHECK(!ticks.has_pair());
}
