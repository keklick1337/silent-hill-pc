// SPDX-License-Identifier: GPL-3.0-or-later
//
// A clip of the game's clip tables as the running game keeps it (a model's base and map clips, Harry's base clips):
// its playback routine, the clip status it plays as, the duration (constant, or a routine at 30 fps) and the keyframe
// span. The layout is the one the animation player reads.
#pragma once

#include <cstdint>

namespace sh::entities {

struct ModelRecord;
}  // namespace sh::entities
namespace sh::camera {
struct FrameRecord;
}  // namespace sh::camera
namespace sh::entities {
using BoneCoord = sh::camera::FrameRecord;

struct ClipRecord {
    void (*playback)(ModelRecord* model, void* anim_file, BoneCoord* bones, ClipRecord* clip);
    std::uint8_t status;
    std::int8_t variable_duration;  // the duration is the routine, not the constant
    std::uint8_t link_status;
    union {
        std::int32_t constant;  // Q12 keyframes per second at 30 fps
        std::int32_t (*variable)(void);
    } duration;
    std::int16_t start_keyframe;  // -1 for none
    std::int16_t end_keyframe;
};

}  // namespace sh::entities
