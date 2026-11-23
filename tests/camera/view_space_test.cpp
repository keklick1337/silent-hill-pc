// The view space of the camera module against hand-worked numbers that pin the rules.

#include "engine/camera/camera_config.h"
#include "engine/camera/view_space.h"

#include "framework/sh_test.h"

namespace cam = sh::camera;

namespace {

constexpr cam::Matrix kIdentity{{{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}}, {0, 0, 0}};

struct Node {
    unsigned flg = 0;
    cam::Matrix local = kIdentity;
    cam::Matrix world{};
    Node* parent = nullptr;
    Node* child = nullptr;
};

class Frames final : public cam::FrameHost {
public:
    static Node* n(Frame f) { return static_cast<Node*>(f); }
    bool usable(Frame f) const override { return f != nullptr; }
    Frame parent(Frame f) override { return n(f)->parent; }
    bool world_current(Frame f) const override { return n(f)->flg != 0; }
    void mark_world_current(Frame f) override { n(f)->flg++; }
    void set_child(Frame f, Frame c) override { n(f)->child = n(c); }
    Frame child(Frame f) const override { return n(f)->child; }
    cam::Matrix local(Frame f) const override { return n(f)->local; }
    cam::Matrix world(Frame f) const override { return n(f)->world; }
    void set_world(Frame f, const cam::Matrix& m) override { n(f)->world = m; }
};

cam::Matrix translated(std::int32_t x, std::int32_t y, std::int32_t z) {
    cam::Matrix m = kIdentity;
    m.t[0] = x;
    m.t[1] = y;
    m.t[2] = z;
    return m;
}

}  // namespace

SH_TEST(view_frame_world_walks_stale_frames_only) {
    Node root, mid, leaf;
    root.local = translated(100, 0, 0);
    mid.local = translated(0, 50, 0);
    leaf.local = translated(0, 0, 7);
    mid.parent = &root;
    leaf.parent = &mid;

    Frames host;
    const cam::Matrix w = cam::frame_world(host, &leaf);
    SH_CHECK(w.t[0] == 100 && w.t[1] == 50 && w.t[2] == 7);
    SH_CHECK(root.flg == 1 && mid.flg == 1 && leaf.flg == 1);
    // The climb threads child links down the chain and clears the root's.
    SH_CHECK(root.child == &mid && mid.child == &leaf && leaf.child == nullptr);

    // A current parent is not recomputed: its cached world is used as is.
    root.world = translated(1000, 0, 0);
    mid.flg = 0;
    leaf.flg = 0;
    const cam::Matrix again = cam::frame_world(host, &leaf);
    SH_CHECK(again.t[0] == 1000 && again.t[1] == 50 && again.t[2] == 7);
    SH_CHECK(root.flg == 1 && mid.flg == 1);

    // An unusable frame gives the identity.
    const cam::Matrix none = cam::frame_world(host, nullptr);
    SH_CHECK(none.m[0][0] == 4096 && none.m[1][1] == 4096 && none.t[0] == 0);
}

SH_TEST(view_world_screen_moves_the_eye_to_the_origin) {
    // Eye at (256, 0, 512) looking down +Z with no rotation.
    const cam::WorldScreen ws = cam::world_screen_from_eye(translated(256, 0, 512));
    SH_CHECK(ws.world_to_view.m[0][0] == 4096 && ws.world_to_view.t[0] == 0);
    SH_CHECK(ws.world_screen.t[0] == -256 && ws.world_screen.t[1] == 0 && ws.world_screen.t[2] == -512);

    // A frame at the eye is at the view origin.
    const cam::Matrix v = cam::view_matrix(ws, translated(256, 0, 512));
    SH_CHECK(v.t[0] == 0 && v.t[1] == 0 && v.t[2] == 0);

    // A frame far from the eye keeps its true offset: 300 m ahead and 600 m to the side do not wrap at 16 bits
    // (a free camera flown away from a character must not see it again at a wrapped place).
    const cam::Matrix far = cam::view_matrix(ws, translated(256 - 600 * 256, 40, 512 + 300 * 256));
    SH_CHECK(far.t[0] == -600 * 256 && far.t[1] == 40 && far.t[2] == 300 * 256);
    // Through a turned eye too: a quarter turn about Y swaps the axes, the lengths stay.
    cam::Matrix turned{{{0, 0, 4096}, {0, 4096, 0}, {-4096, 0, 0}}, {0, 0, 0}};
    const cam::WorldScreen side = cam::world_screen_from_eye(turned);
    const cam::Matrix seen = cam::view_matrix(side, translated(1200 * 256, 0, 0));
    SH_CHECK(seen.t[0] == 0 && seen.t[1] == 0 && seen.t[2] == 1200 * 256);

    // world_screen_at folds a Q12 position in (4096 = 1 m = 256 geometry units).
    const cam::Matrix at = cam::world_screen_at(ws.world_screen, cam::Vec3{4096, 0, 4096});
    SH_CHECK(at.t[0] == 0 && at.t[2] == -256);
}

SH_TEST(view_eye_placements) {
    // Looking along +X from the origin: yaw a quarter turn, translation in geometry units.
    const cam::Matrix look = cam::look_at_matrix(cam::Vec3{4096, 0, 0}, cam::Vec3{8192, 0, 0});
    SH_CHECK(look.m[0][2] == 4096 && look.t[0] == 256);
    const cam::ViewPose pose = cam::pose_of(look);
    SH_CHECK(pose.angles.y == 1024 && pose.position.x == 4096);

    // Orbit: 1 m behind on yaw 0, level: the eye sits at +Z and turns to face -Z.
    const cam::Matrix orbit = cam::orbit_matrix(cam::Vec3{0, 0, 0}, 0, 0, 0, 4096);
    SH_CHECK(orbit.t[0] == 0 && orbit.t[1] == 0 && orbit.t[2] == 256);
    SH_CHECK(cam::pose_of(orbit).angles.y == -2048 || cam::pose_of(orbit).angles.y == 2048);

    // The reference view: direction kept in 16 bits, no roll.
    const cam::Matrix ref = cam::reference_view_local(cam::Vec3{10, 20, 30}, cam::Vec3{10, 20, 31});
    SH_CHECK(ref.m[0][0] == 4096 && ref.t[0] == 10 && ref.t[2] == 30);
}

SH_TEST(view_angle_range_and_segment_rules) {
    // [100, 300] narrowed by [200, 400] -> [200, 300].
    std::int16_t lo = 100, hi = 300;
    cam::clamp_angle_range(lo, hi, 200, 400);
    SH_CHECK(lo == 200 && hi == 300);
    // No overlap: collapses onto the limit's start... clamped to the arc's end.
    lo = 100;
    hi = 300;
    cam::clamp_angle_range(lo, hi, 1000, 1200);
    SH_CHECK(lo == 300 && hi == 300);
    // Wrapping limit [4000, 200] around min = 100: only the upper end 200 limits.
    lo = 100;
    hi = 300;
    cam::clamp_angle_range(lo, hi, 4000, 200);
    SH_CHECK(lo == 100 && hi == 200);

    // Heading away from the line: no crossing.
    SH_CHECK(cam::segment_cross_fraction(100, 10, -5, -100, 100) == 4096);
    // Halfway: distance 5 over direction 10 -> 0.5.
    SH_CHECK(cam::segment_cross_fraction(100, 10, 5, -100, 100) == 2048);
    // On the line: 0 inside the bounds, 1.0 outside.
    SH_CHECK(cam::segment_cross_fraction(100, 10, 0, -1, 1) == 0);
    SH_CHECK(cam::segment_cross_fraction(100, 10, 0, 1, 2) == 4096);
    // Crossing point outside the bounds along the line.
    SH_CHECK(cam::segment_cross_fraction(1000, 10, 5, -100, 100) == 4096);
}

SH_TEST(view_region_span) {
    std::uint8_t r[3][3] = {};
    SH_CHECK(!cam::regions_span_screen(r));
    r[1][1] = 1;
    SH_CHECK(cam::regions_span_screen(r));
    r[1][1] = 0;
    r[1][0] = 1;
    SH_CHECK(!cam::regions_span_screen(r));
    r[0][2] = 1;
    r[2][2] = 1;  // left and right (via both corners): spans
    SH_CHECK(cam::regions_span_screen(r));
}

SH_TEST(view_box_tests) {
    cam::ScreenProjection proj;
    proj.h = 224;

    // A 1 m box 4 m in front of the eye is on the screen; the same box behind it is not.
    const cam::Matrix ws = kIdentity;
    SH_CHECK(cam::box_on_screen(ws, proj, 320, 224, -2048, 2048, -2048, 2048, 4 * 4096, 5 * 4096));
    SH_CHECK(!cam::box_on_screen(ws, proj, 320, 224, -2048, 2048, -2048, 2048, -5 * 4096, -4 * 4096));
    // Far to the side: off the 4:3 screen.
    SH_CHECK(!cam::box_on_screen(ws, proj, 320, 224, 40 * 4096, 41 * 4096, -2048, 2048, 4 * 4096, 5 * 4096));

    cam::ScreenGeometry g;  // 4:3, no widening
    const cam::CullBounds b = cam::cull_bounds(g);
    SH_CHECK(b.half_width == 160 + 64 && b.half_height == 112 + 64);
    g.render_width = 640;
    g.render_height = 360;  // 16:9 at twice the width
    const cam::CullBounds wide = cam::cull_bounds(g);
    SH_CHECK(wide.hor_scale == 2.0f && wide.half_width > b.half_width);

    // Draw test: box in front -> visible; wholly beyond the far depth -> hidden; a corner nearer
    // than the near depth -> visible.
    cam::Matrix mv = translated(0, 0, 2000);
    SH_CHECK(cam::box_in_frustum(mv, proj, b, -100, -100, -100, 100, 100, 100, 6400, 224));
    mv.t[2] = 30000;
    SH_CHECK(!cam::box_in_frustum(mv, proj, b, -100, -100, -100, 100, 100, 100, 6400, 224));
    mv.t[2] = 100;
    SH_CHECK(cam::box_in_frustum(mv, proj, b, -100, -100, -100, 100, 100, 100, 6400, 224));
    // Off to the right of the bounds.
    mv = translated(20000, 0, 2000);
    SH_CHECK(!cam::box_in_frustum(mv, proj, b, -100, -100, -100, 100, 100, 100, 6400, 224));
}

SH_TEST(view_camera_config) {
    const auto def = cam::parse_camera_config("{}");
    SH_CHECK(def && def.value().draw_cull_margin == 64);
    const auto set = cam::parse_camera_config(R"({"view": {"draw_cull_margin": 16}})");
    SH_CHECK(set && set.value().draw_cull_margin == 16);
    SH_CHECK(!cam::parse_camera_config(R"({"view": {"draw_cull_margin": -1}})"));
    SH_CHECK(!cam::parse_camera_config("{"));
}
