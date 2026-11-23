/* The story-character helpers of a map with a cast (engine/maps/chara_path_walk.h, chara_rules.h): the waypoint
 * walk, the shortest turn, the character -> animation table lookup, the cue sound rules and the keyframe cue. */
#include "engine/maps/chara_path_walk.h"
#include "engine/maps/chara_rules.h"
#include "framework/sh_test.h"

using sh::maps::PathWalk;
using sh::maps::PathWalkActor;
using sh::maps::WalkPoint;

SH_TEST(chara_path_walk_shortest_turn_wraps) {
    SH_CHECK(PathWalk::shortest_turn(0, 100) == 100);
    SH_CHECK(PathWalk::shortest_turn(100, 0) == -100);
    SH_CHECK(PathWalk::shortest_turn(100, 4000) == -196);
    SH_CHECK(PathWalk::shortest_turn(4000, 100) == 196);
}

SH_TEST(chara_path_walk_turns_then_walks_to_the_point) {
    PathWalk walk;
    PathWalkActor a;
    a.yaw = 2048;  // facing away from the point
    const WalkPoint pts[] = {{0, 40960}};
    auto at = [&](int i) { return pts[i]; };
    SH_CHECK(!walk.step(a, 2, at, 0, 1, 136));  // phase 0 -> 1
    SH_CHECK(a.phase == 1);
    SH_CHECK(!walk.step(a, 2, at, 0, 1, 136));  // must turn first
    SH_CHECK(a.phase == 2);
    SH_CHECK(a.anim == PathWalk::kTurnLeftAnim || a.anim == PathWalk::kTurnRightAnim);
    a.yaw = 0;
    SH_CHECK(!walk.step(a, 2, at, 0, 1, 136));  // faced the point
    SH_CHECK(a.phase == 3);
    SH_CHECK(!walk.step(a, 2, at, 0, 1, 136));  // starts walking
    SH_CHECK(a.phase == 4 && a.anim == 2 && a.anim_request == 2);
    a.z = 40960;  // arrives
    SH_CHECK(!walk.step(a, 2, at, 0, 1, 136));
    SH_CHECK(!walk.step(a, 2, at, 0, 1, 136));
    SH_CHECK(a.phase == 6 && walk.next_point() == 1);
    SH_CHECK(!walk.step(a, 2, at, 0, 1, 136));  // already facing the end heading
    SH_CHECK(a.phase == 8);
    SH_CHECK(walk.step(a, 2, at, 0, 1, 136));
    SH_CHECK(a.arrived == 1 && a.phase == 0 && a.stop_motion);
}

SH_TEST(chara_path_walk_standing_on_the_first_point_skips_the_walk) {
    PathWalk walk;
    PathWalkActor a;
    a.x = 100;
    a.z = 200;
    a.phase = 1;
    const WalkPoint pts[] = {{100, 200}};
    SH_CHECK(!walk.step(a, 1, [&](int i) { return pts[i]; }, 1024, 1, 136));
    SH_CHECK(a.phase == 6);
}

SH_TEST(chara_table_lookup_listed_then_fallback) {
    sh::maps::CharaTableLookup l;
    SH_CHECK(l.find(26) == sh::maps::CharaTableLookup::kNone);
    l.set(26, 0);
    l.set_fallback(1);
    SH_CHECK(l.find(26) == 0);
    SH_CHECK(l.find(30) == 1);
    SH_CHECK(l.find(static_cast<std::uint8_t>(-5)) == 1);
}

SH_TEST(chara_cue_sound_rules_default_and_listed) {
    sh::maps::CueSoundRules r;
    r.set(1492, {255, 2});
    SH_CHECK(r.find(1492).volume == 255 && r.find(1492).flags == 2);
    SH_CHECK(r.find(1300).volume == 128 && r.find(1300).flags == 0);
}

SH_TEST(chara_keyframe_cue_fires_once_and_rearms) {
    std::int32_t flags = 0;
    SH_CHECK(!sh::maps::keyframe_cue(4, 5, flags, 16));
    SH_CHECK(sh::maps::keyframe_cue(5, 5, flags, 16));
    SH_CHECK(flags == 16);
    SH_CHECK(!sh::maps::keyframe_cue(6, 5, flags, 16));
    SH_CHECK(!sh::maps::keyframe_cue(0, 5, flags, 16));
    SH_CHECK(flags == 0);
}
