// Harry's self-view head gaze against hand-worked numbers that pin the rules.

#include "engine/camera/self_view_gaze.h"

#include "framework/sh_test.h"

namespace cam = sh::camera;

namespace {

cam::SelfViewGazeInput gaze_in(int anim, bool active, std::int16_t body_yaw, cam::Angles head, bool enemy, int pitch) {
    cam::SelfViewGazeInput in;
    in.anim_status = anim * 2 + (active ? 1 : 0);
    in.body_yaw = body_yaw;
    in.head = head;
    in.enemy_near = enemy;
    in.pitch = pitch;
    return in;
}

bool is(const cam::Angles& a, int x, int y, int z) { return a.x == x && a.y == y && a.z == z; }

}  // namespace

SH_TEST(self_view_gaze_idle_and_walk) {
    const cam::SelfViewGazeAnims anims;
    // Idle: a 100-unit head turn past the 45-unit dead zone becomes 45 + 55/8; pitch -91, plus pitch/2.
    SH_CHECK(is(cam::self_view_gaze(gaze_in(26, true, 1000, {10, 1100, 20}, false, 40), anims), 10 - 91 + 20, 1051, 10));
    SH_CHECK(is(cam::self_view_gaze(gaze_in(26, false, 1000, {0, 1030, 0}, false, 0), anims), -91, 1030, 0));
    SH_CHECK(is(cam::self_view_gaze(gaze_in(26, false, 1000, {0, 900, 0}, false, 0), anims), -91, 1000 - 51, 0));
    // Exhausted idle: its yaw is the body's, and its pitch is the look-at pitch scaled alone (the
    // halved, lowered bone pitch is overwritten).
    SH_CHECK(is(cam::self_view_gaze(gaze_in(27, true, 500, {100, 700, 0}, false, 400), anims), 279, 500, 0));
    // Walking eases the turn by 1/8 within +-113; the inactive walk status keeps the bone pitch.
    SH_CHECK(is(cam::self_view_gaze(gaze_in(2, false, 0, {30, 2000, 8}, false, 10), anims), 35, 113, 4));
    SH_CHECK(is(cam::self_view_gaze(gaze_in(2, false, 0, {30, -200, 8}, false, 10), anims), 35, -25, 4));
    // Running keeps the roll.
    SH_CHECK(is(cam::self_view_gaze(gaze_in(3, true, 0, {30, 80, 8}, false, 0), anims), 30, 10, 8));
}

SH_TEST(self_view_gaze_look_turn_fall_other) {
    const cam::SelfViewGazeAnims anims;
    // Looking around: at an enemy the head faces the body, level at -79 and without roll.
    SH_CHECK(is(cam::self_view_gaze(gaze_in(17, true, 300, {200, 900, 40}, true, 20), anims), -79 + 10, 300, 0));
    SH_CHECK(is(cam::self_view_gaze(gaze_in(17, true, 300, {200, 900, 40}, false, 20), anims), 200 - 91 + 10, 900 + 341, 40));
    // Turning: heading sector 0 or 5 dips one more degree.
    SH_CHECK(cam::self_view_gaze(gaze_in(12, true, 5 << 7, {0, 640, 0}, false, 0), anims).x == -11 - 68);
    SH_CHECK(cam::self_view_gaze(gaze_in(13, true, 1 << 7, {0, 128, 0}, false, 0), anims).x == -68);
    // A fall keeps the bone yaw; other animations scale the pitch alone by 252 degrees.
    SH_CHECK(is(cam::self_view_gaze(gaze_in(22, false, 0, {50, 777, 6}, false, 4096), anims), 2867, 777, 3));
    SH_CHECK(is(cam::self_view_gaze(gaze_in(40, false, 99, {50, 777, -7}, false, -4096), anims), -2867, 99, -4));
    // The numbers come from the data: a renumbered idle is recognised by its new index only.
    cam::SelfViewGazeAnims moved;
    moved.idle = 50;
    SH_CHECK(cam::self_view_gaze(gaze_in(26, false, 0, {0, 0, 0}, false, 0), moved).x == 0);
    SH_CHECK(cam::self_view_gaze(gaze_in(50, false, 0, {0, 0, 0}, false, 0), moved).x == -91);
}
