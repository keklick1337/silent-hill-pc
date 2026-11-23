// The camera's matrix helpers and parts of stage C against hand-worked numbers.

#include "engine/camera/camera_matrix.h"
#include "engine/camera/camera_rig.h"

#include "framework/sh_test.h"

namespace cam = sh::camera;

SH_TEST(camera_rotation_identity_and_quarter_yaw) {
    const cam::Matrix id = cam::rotation_yxz(cam::Angles{0, 0, 0});
    SH_CHECK(id.m[0][0] == 4096 && id.m[1][1] == 4096 && id.m[2][2] == 4096);
    SH_CHECK(id.m[0][1] == 0 && id.m[0][2] == 0 && id.m[1][2] == 0);

    // A quarter turn of yaw: X goes to -Z, Z goes to +X; the near-zero cosine truncates to 0.
    const cam::Matrix yaw = cam::rotation_yxz(cam::Angles{0, 1024, 0});
    SH_CHECK(yaw.m[0][2] == 4096);
    SH_CHECK(yaw.m[2][0] == -4096);
    SH_CHECK(yaw.m[0][0] == 0 && yaw.m[2][2] == 0);
}

SH_TEST(camera_matrix_multiply_transpose_apply) {
    const cam::Matrix yaw = cam::rotation_yxz(cam::Angles{0, 1024, 0});
    const cam::Matrix back = cam::multiply(cam::transpose(yaw), yaw);
    SH_CHECK(back.m[0][0] == 4096 && back.m[1][1] == 4096 && back.m[2][2] == 4096);
    SH_CHECK(back.m[0][2] == 0 && back.m[2][0] == 0);

    const cam::Angles v = cam::apply(yaw, cam::Angles{0, 0, 100});
    SH_CHECK(v.x == 100 && v.y == 0 && v.z == 0);

    // The product saturates to 16 bits.
    cam::Matrix big;
    big.m[0][0] = 32767;
    big.m[0][1] = 32767;
    const cam::Angles sat = cam::apply(big, cam::Angles{32767, 32767, 0});
    SH_CHECK(sat.x == 32767);
}

SH_TEST(camera_vector_and_matrix_angles) {
    const cam::Angles ahead = cam::vector_angles(cam::Angles{0, 0, 4096});
    SH_CHECK(ahead.x == 0 && ahead.y == 0 && ahead.z == 0);
    const cam::Angles right = cam::vector_angles(cam::Angles{4096, 0, 0});
    SH_CHECK(right.y == 1024);
    const cam::Angles up = cam::vector_angles(cam::Angles{0, -4096, 0});
    SH_CHECK(up.x == 1024);

    const cam::Angles none = cam::matrix_angles(cam::rotation_yxz(cam::Angles{0, 0, 0}));
    SH_CHECK(none.x == 0 && none.y == 0 && none.z == 0);
    const cam::Angles quarter = cam::matrix_angles(cam::rotation_yxz(cam::Angles{0, 1024, 0}));
    SH_CHECK(quarter.y == 1024 && quarter.x == 0);
}

SH_TEST(camera_frame_end_clears_warps_and_reports_settle) {
    cam::CameraRig rig;
    rig.set_roads({}, false);
    cam::CameraState& st = rig.state();
    // A script owns both the camera and the look-at, the camera sits still.
    st.flags = cam::camera_flags::user_camera | cam::camera_flags::user_watch | cam::camera_flags::warp_camera |
               cam::camera_flags::warp_watch;
    st.through_door_activate = false;
    st.watch_target_pos = cam::Vec3{0, 0, 4096 * 4};
    cam::FrameInput in;
    in.delta_time = 68;
    in.screen_width = 320;
    in.screen_height = 240;
    const cam::FrameResult r = rig.update(in);
    SH_CHECK((st.flags & (cam::camera_flags::warp_camera | cam::camera_flags::warp_watch |
                          cam::camera_flags::warp_camera_target)) == 0);
    SH_CHECK(r.view.view.kind == cam::ViewOutput::Kind::DirectMatrix);
    // Looking straight down +Z from the origin: identity rotation, no movement -> settle.
    SH_CHECK(r.view.view.matrix.m[0][0] == 4096 && r.view.view.matrix.m[2][2] == 4096);
    SH_CHECK(r.view.smooth_move_type == 1);
}
