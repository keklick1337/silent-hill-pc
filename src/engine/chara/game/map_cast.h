// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The story-character helpers a map with a cast hands its events: control state changes,
// animation lock and playback state, the waypoint walk, the start keyframe of the current clip and the collision
// reset. The cast is data, gamedata/maps/<map>/charas.json (which clip table each character's status indexes, the
// volumes of the cue sounds), read when the map loads; a map without that file has none of the helpers.
//
// The C ABI of the map header calls these through its slots: a bool argument or result is the C code's 4-byte one.

#pragma once

#include <cstdint>

#include "engine/entities/character_record.h"

namespace sh::chara::game::cast {

using sh::entities::CharacterRecord;

// Reads the map's charas.json; true when the map has a cast. The walk's shared words start at zero with every map.
bool load(const char* map);

// The character's cutscene control ends: with `to_still` its animation restarts from the still pose, otherwise its
// control state starts over.
void control_end(CharacterRecord* chara, std::uint32_t to_still);
// A new control state for the character.
void control_state_set(CharacterRecord* chara, std::int32_t state);
// The character's update re-enters its control state.
void control_reset(CharacterRecord* chara);
void anim_lock(CharacterRecord* chara);
void anim_unlock(CharacterRecord* chara);
// Whether the character's one-shot animation has played to its end (1/0), -2 while it blends, -1 for any other
// playback or a character without a table on this map.
std::int32_t anim_playback_state(CharacterRecord* chara);
// One step of a walk through `count` waypoints with the walking animation `walk_anim`, ending with the heading
// `final_heading`. 1 on the call the walk ends.
std::uint32_t path_step(CharacterRecord* chara, std::int32_t walk_anim, const sh::entities::Vec3* points,
                        std::int32_t final_heading, std::int32_t count);
// The first keyframe of the character's current clip.
std::int32_t anim_start_keyframe(CharacterRecord* chara);
// The collision shape and the movement words go back to zero and the control state advances.
void collision_reset(CharacterRecord* chara);

}  // namespace sh::chara::game::cast
