// The loading screen's watch and camera targets (engine/gameflow/load_screen_pose.h): recorded positions and yaws,
// a long seeded sweep against its recorded digest, the rule in words, and the config file's checks.

#include <string>

#include "engine/gameflow/load_screen_pose.h"

#include "framework/golden.h"
#include "framework/sh_test.h"

namespace gf = sh::gameflow;

namespace {
constexpr const char* kTable = "gameflow/load_screen_view.txt";
}

SH_TEST(load_screen_view_as_recorded) {
    const gf::LoadScreenParams p = gf::LoadScreenParams::builtin();
    const auto rows = sh::test::golden_rows(kTable, "view");
    SH_CHECK(rows.size() == 400);
    int differences = 0;
    for (const auto& r : rows) {
        const gf::LoadScreenView v = gf::load_screen_view(p, static_cast<std::int32_t>(r.num(0)),
                                                          static_cast<std::int32_t>(r.num(1)),
                                                          static_cast<std::int32_t>(r.num(2)));
        if (v.look_at.x != r.num(3) || v.look_at.y != r.num(4) || v.look_at.z != r.num(5) ||
            v.cam_target.x != r.num(6) || v.cam_target.y != r.num(7) || v.cam_target.z != r.num(8)) {
            if (++differences <= 3) {
                sh::test::golden_mismatch(kTable, r,
                                          "the camera target " + std::to_string(v.cam_target.x) + " " +
                                              std::to_string(v.cam_target.y) + " " + std::to_string(v.cam_target.z));
            }
        }
    }
    SH_CHECK(differences == 0);
}

SH_TEST(load_screen_view_seeded_sweep_digest) {
    const gf::LoadScreenParams p = gf::LoadScreenParams::builtin();
    const auto rows = sh::test::golden_rows(kTable, "digest");
    SH_CHECK(rows.size() == 1);
    for (const auto& r : rows) {
        sh::test::Sequence q(static_cast<std::uint64_t>(r.num(0)));
        sh::test::Digest d;
        for (std::int64_t i = 0; i < r.num(1); ++i) {
            const std::int32_t x = q.below(2000000) - 1000000;
            const std::int32_t z = q.below(2000000) - 1000000;
            const std::int32_t yaw = q.below(8192) - 4096;
            const gf::LoadScreenView v = gf::load_screen_view(p, x, z, yaw);
            d.add(v.look_at.x);
            d.add(v.look_at.y);
            d.add(v.look_at.z);
            d.add(v.cam_target.x);
            d.add(v.cam_target.y);
            d.add(v.cam_target.z);
        }
        SH_CHECK(sh::test::golden_digest_is(kTable, r, 2, d));
    }
}

// The camera watches the player at chest height from two metres away, a little off the direction he faces
// (22.5 degrees clockwise) and one metre up; the heights do not depend on where he stands.
SH_TEST(load_screen_view_stands_beside_the_player) {
    const gf::LoadScreenParams p = gf::LoadScreenParams::builtin();
    // Yaw 256 cancels the offset: the camera is straight behind along z.
    gf::LoadScreenView v = gf::load_screen_view(p, 1000, 5000, 256);
    SH_CHECK(v.look_at.x == 1000 && v.look_at.y == -2457 && v.look_at.z == 5000);
    SH_CHECK(v.cam_target.x == 1000 && v.cam_target.y == -4096 && v.cam_target.z == 5000 - 2 * 4096);
    // A quarter turn further it is to the side along x.
    v = gf::load_screen_view(p, 1000, 5000, 256 + 1024);
    SH_CHECK(v.cam_target.x == 1000 - 2 * 4096 && v.cam_target.z == 5000);
    // The parameters move it.
    gf::LoadScreenParams far = p;
    far.cam_distance_mul = 3;
    far.cam_angle_offset = 0;
    far.cam_height = -100;
    far.look_height = -7;
    v = gf::load_screen_view(far, 0, 0, 0);
    SH_CHECK(v.cam_target.x == 0 && v.cam_target.y == -100 && v.cam_target.z == -3 * 4096 && v.look_at.y == -7);
}

SH_TEST(load_screen_params_parse) {
    const auto ok = gf::LoadScreenParams::parse(R"({"pose_time": 5, "cam_distance_mul": 3})");
    SH_CHECK(ok && ok.value().pose_time == 5 && ok.value().cam_distance_mul == 3 && ok.value().cam_height == -4096);
    SH_CHECK(!gf::LoadScreenParams::parse(R"({"pose_time": "x"})"));
}
