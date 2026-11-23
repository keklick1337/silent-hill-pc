// The view system's state and step order (engine/camera/view_system.h) on the neutral camera host.

#include "engine/camera/camera_host.h"
#include "engine/camera/view_system.h"

#include "framework/sh_test.h"

namespace cam = sh::camera;

namespace {

constexpr cam::Matrix kIdentity{{{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}}, {0, 0, 0}};

}  // namespace

SH_TEST(view_system_init_is_an_identity_root_at_the_origin) {
    cam::ViewSystem view;
    view.init();
    SH_CHECK(view.frame()->world_current == 1 && view.frame()->parent == nullptr);
    SH_CHECK(view.position().x == 0 && view.position().y == 0 && view.position().z == 0);
    SH_CHECK(view.angles().x == 0 && view.angles().y == 0 && view.angles().z == 0);
    // The reference view looks straight ahead from the frame: the eye sits at the frame's origin.
    SH_CHECK(view.eye().t[0] == 0 && view.eye().t[1] == 0 && view.eye().t[2] == 0);
    // The world-to-view rotation is the eye's rotation transposed.
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            SH_CHECK(view.world_to_view().m[r][c] == view.eye().m[c][r]);
        }
    }
}

SH_TEST(view_system_update_reads_the_pose_of_the_placed_frame) {
    cam::ViewSystem view;
    view.init();
    cam::Matrix placed = kIdentity;
    placed.t[0] = 256;  // geometry units: 1 m = 256 (Q12 >> 4)
    placed.t[2] = -512;
    view.set_frame(nullptr, placed);
    SH_CHECK(view.frame()->world_current == 0);
    view.update();
    SH_CHECK(view.frame()->world_current == 1);
    const cam::ViewPose pose = cam::pose_of(view.frame()->world);
    SH_CHECK(view.position().x == pose.position.x && view.position().z == pose.position.z);
    SH_CHECK(view.position().x > 0 && view.position().z < 0);
    // The renderer's world-screen matrix moves the eye back to the origin.
    const cam::Matrix& ws = cam::camera_host().world_screen();
    SH_CHECK(ws.t[0] == -256 && ws.t[2] == 512);
}

SH_TEST(view_system_frame_walk_stops_at_wild_links) {
    cam::FrameRecord root{};
    root.local = kIdentity;
    root.local.t[1] = 77;
    cam::FrameRecord leaf{};
    leaf.local = kIdentity;
    leaf.parent = &root;
    SH_CHECK(cam::frame_world(&leaf).t[1] == 77);

    // A truncated pointer ends the chain: the leaf is its own root.
    cam::FrameRecord orphan{};
    orphan.local = kIdentity;
    orphan.local.t[2] = 5;
    orphan.parent = reinterpret_cast<cam::FrameRecord*>(static_cast<std::uintptr_t>(0x1234));
    const cam::Matrix w = cam::frame_world(&orphan);
    SH_CHECK(w.t[2] == 5);
    SH_CHECK(!cam::FrameRecords::canonical(nullptr));
}
