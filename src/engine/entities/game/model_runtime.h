// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The animation and skeleton entry points the game's character code calls: bind pose,
// clip playback, bone sampling, the skeleton part list. They work on the engine's records (character_record.h,
// world/skeleton.h), whose layouts match the game's records.

#pragma once

#include <cstdint>

#include "engine/entities/character_record.h"

namespace sh::entities {

struct ClipInfoRecord;

// The playback a clip table entry names (one of the Anim_* entry points below).
using ClipPlayback = void (*)(ModelRecord* model, void* anm_header, BoneCoord* bones, ClipInfoRecord* clip);
// A clip whose length depends on the character's state (Harry's run): gets the model and the entry.
using ClipDurationFn = std::int32_t (*)(ModelRecord* model, ClipInfoRecord* clip);

// One entry of a character's clip table, indexed by the packed animation status.
struct ClipInfoRecord {
    ClipPlayback playback;
    std::uint8_t status;
    std::int8_t variable_duration;  // the duration is a function
    std::uint8_t next_status;       // where a blend or a one-shot goes when it ends
    union {
        std::int32_t constant;      // Q12 keyframes per second
        void* function;             // ClipDurationFn
    } duration;
    std::int16_t start_keyframe;
    std::int16_t end_keyframe;
};

}  // namespace sh::entities

