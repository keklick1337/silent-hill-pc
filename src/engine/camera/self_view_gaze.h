#pragma once

// Where Harry's head looks in the self view: his head bone's
// angles, settled toward his body yaw and pitched toward the current look-at, by the animation he
// plays. The animation numbers come by name from gamedata/chara/Harry/character.json "anims"; the
// game reads his record and the head bone and hands both in.

#include <cstdint>

#include "engine/camera/camera_types.h"

namespace sh::camera {

// Harry's animation indices the gaze tells apart (the shipped "anims" numbers are the defaults).
struct SelfViewGazeAnims {
    int walk_forward = 2;
    int run_forward = 3;
    int turn_left = 12;
    int turn_right = 13;
    int look_around = 17;
    int fall_forward = 22;
    int fall_backward = 23;
    int idle = 26;
    int idle_exhausted = 27;
};

struct SelfViewGazeInput {
    // The model's animation status: the animation index shifted up by one, the low bit set while
    // it is active.
    std::int32_t anim_status = 0;
    std::int16_t body_yaw = 0;
    Angles head{};          // the head bone's world angles (YXZ)
    bool enemy_near = false;
    std::int32_t pitch = 0;  // the pitch toward the current look-at
};

Angles self_view_gaze(const SelfViewGazeInput& in, const SelfViewGazeAnims& anims);

}  // namespace sh::camera
