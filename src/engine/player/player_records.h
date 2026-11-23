// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Records the player's host (src/engine/entities/game/player_host.cpp) shares with other modules: a clip table
// entry as the playback code reads it, Harry's variable block of his character record, and the ground the
// collision reports. Their byte layouts match those modules' records.

#pragma once

#include <cstdint>

#include "engine/entities/character_record.h"

namespace sh::game {

// One entry of a clip table (the game's clip record): the playback, the status it hands over to, the duration
// (constant Q12 keyframes per second, or a function) and the keyframe range.
struct ClipTableEntry {
    void (*playback)(void* model, void* anim_file, void* bones, ClipTableEntry* entry);
    std::uint8_t status;
    std::int8_t has_variable_duration;
    std::uint8_t link_status;
    union {
        std::int32_t constant;
        void* variable;
    } duration;
    std::int16_t start_keyframe;
    std::int16_t end_keyframe;
};

using HarryVars = sh::entities::HarryVars;

inline HarryVars& harry_vars(sh::entities::CharacterRecord& r) { return r.vars.harry; }

// What the ray trace reports: a hit, the point, the character met (its record) and the ground there.
struct RayHit {
    std::int8_t has_hit;
    std::uint8_t ground_type;
    std::int8_t reserved[2];
    std::int32_t target[3];
    const void* character;
    std::int32_t distance;
    std::int32_t ground_height;
    std::int16_t angle;
};

// The ground at a point as the collision reports it.
struct GroundRecord {
    std::int32_t height;
    std::int16_t tilt_x, tilt_z;
    std::int8_t type;
    std::uint8_t reserved[3];
};

}  // namespace sh::game
