// Arrival after a room or map change against its specification (the original's door code): the
// walk-through offset and the 0.4 m step ahead, values worked out by hand.

#include "engine/gameflow/arrival.h"

#include "framework/sh_test.h"

namespace gf = sh::gameflow;

SH_TEST(arrival_point_keeps_offset_only_for_walk_through) {
    gf::ArrivalPoint target{10000, 20000, 0, 1};
    auto a = gf::arrival_point(target, 500, 600, 700, 400);
    SH_CHECK(a.x == 10200 && a.z == 19800);
    target.half_width_q4 = 2;
    a = gf::arrival_point(target, 500, 600, 700, 400);
    SH_CHECK(a.x == 10000 && a.z == 20000);
}

SH_TEST(arrival_pose_heading_and_step_ahead) {
    // heading_q8 = 64: a quarter turn, heading 1024; sin = 4096, cos = 0 (6.1e-17 * 4096 truncates).
    gf::ArrivalPoint p{0, 0, 64, 2};
    auto pose = gf::arrival_pose(p);
    SH_CHECK(pose.heading == 1024 && pose.x == 1638 && pose.z == 0);
    p.half_width_q4 = 1;
    pose = gf::arrival_pose(p);
    SH_CHECK(pose.x == 0 && pose.z == 0);
    p.heading_q8 = 255;
    SH_CHECK(gf::arrival_pose(p).heading == 4080);
}
