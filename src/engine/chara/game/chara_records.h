// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The character slots of the session as the character hosts address them (src/engine/chara/game): slot 0 the
// player, 1.. the NPCs, and -1 the record the original read when Harry's grab index is unset (the console bytes
// before the NPC table, console_character.h).

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/entities/character_record.h"
#include "engine/entities/console_character.h"
#include "engine/game_state/session.h"

namespace sh::chara::game {

inline sh::entities::CharacterSlots& slots() { return sh::game::session().system.characters; }

// Slot -1 is a view decoded from the player's record (console_character.h): a change to it reaches the player
// only through commit_unset_slot().
inline sh::entities::CharacterRecord* record(int slot) {
    if (slot == -1) {
        return &sh::entities::console::unset_slot(slots());
    }
    return slots().record(slot);
}

inline void commit_unset_slot() { sh::entities::console::unset_slot_commit(slots()); }

inline int slot_of(const void* chara) { return slots().slot_of(chara); }

// A word of a record's behaviour variables (the type's block, by byte offset).
template <typename T>
T var_read(const sh::entities::CharacterRecord& r, std::size_t offset) {
    T v;
    std::memcpy(&v, r.vars.bytes + offset, sizeof(T));
    return v;
}

template <typename T>
void var_write(sh::entities::CharacterRecord& r, std::size_t offset, T v) {
    std::memcpy(r.vars.bytes + offset, &v, sizeof(T));
}

// Byte offsets in the variable block of the NPC layout every story character and the base API share.
namespace npc_vars {
constexpr std::size_t kControlState = 0x00;
constexpr std::size_t kWalkPhase = 0x04;
constexpr std::size_t kFreeze = 0x08;
constexpr std::size_t kWalkDone = 0x0C;
constexpr std::size_t kResetControlState = 0x10;
constexpr std::size_t kWalkAnim = 0x14;
constexpr std::size_t kTimer = 0x20;
constexpr std::size_t kDistanceToPlayer = 0x28;
constexpr std::size_t kMonsterCybilWalking = 0x04;  // s16: the walk cycle is running
constexpr std::size_t kMonsterCybilWander = 0x2E;   // u8: 1 while she wanders off
constexpr std::size_t kFlags = 0x34;
constexpr std::size_t kShotFired = 0x38;
constexpr std::size_t kMoveDistance = 0x3C;
constexpr std::size_t kMoveSpeed = 0x3E;
}  // namespace npc_vars

// Harry's values read by name on any record (the collision reset clears his first words on every NPC too).
namespace player_vars {
constexpr std::size_t kAfkTimer = offsetof(sh::entities::HarryVars, afk_timer);
constexpr std::size_t kGroundHeight = offsetof(sh::entities::HarryVars, ground_height);
constexpr std::size_t kFlexAngleX = offsetof(sh::entities::HarryVars, flex_target_x);
constexpr std::size_t kHeadingAngle = offsetof(sh::entities::HarryVars, heading);
}  // namespace player_vars

}  // namespace sh::chara::game
