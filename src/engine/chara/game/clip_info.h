// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// A clip entry of a character's clip table (docs/FORMATS.md): how the model plays the clip of one animation status
// (blend into it, play once, loop), the status it links to, its speed and its keyframe range.
//
// The animation code walks these tables and calls their playback functions, so the entry keeps the layout it reads
// and the playback is the function the entry names. The engine fills the tables from data and reads and writes
// their fields.

#pragma once

#include <cstdint>

#include "engine/entities/character_record.h"

struct _SubCharacter;
struct _AnmHeader;
struct _GsCOORDINATE2;

namespace sh::chara::game {

struct ClipInfo;

// One tick of a clip on a model, its skeleton's animation and bones.
using ClipPlayback = void (*)(sh::entities::ModelRecord* model, void* animation, sh::entities::BoneCoord* bones,
                              ClipInfo* clip);
// A speed that changes while the clip plays (keyframes per second, Q12).
using ClipSpeedFn = std::int32_t (*)(sh::entities::ModelRecord* model, ClipInfo* clip);

struct ClipInfo {
    ClipPlayback playback;  // nullptr: the status poses nothing
    std::uint8_t status;    // the packed status the entry plays (index << 1 | playing)
    std::int8_t variable_speed;  // 1: `speed_fn` gives the speed
    std::uint8_t link;      // the status the clip hands over to at its end
    union {
        std::int32_t speed;  // keyframes per second, Q12
        ClipSpeedFn speed_fn;
    };
    std::int16_t start_keyframe;
    std::int16_t end_keyframe;
};

// A clip table: the entries of one character's statuses.
struct ClipTableRef {
    ClipInfo* clips = nullptr;
    int count = 0;
};

// The update a map header holds for a character type (the NPC loop calls it each tick).
using CharaUpdate = void (*)(_SubCharacter* chara, _AnmHeader* animation, _GsCOORDINATE2* bones);

}  // namespace sh::chara::game
