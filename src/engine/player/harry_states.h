// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Harry's state numbers the engine's Player tests (the values Lua sees as h.state / h.upper_body /
// h.lower_body, gamedata/chara/Harry/state.lua), and the constants of his rig.

#pragma once

#include <cstdint>

namespace sh::game::harry {

// Whole-body states.
namespace state {
constexpr int kNone = 0;  // free movement
constexpr int kCombat = 1;
constexpr int kIdle = 2;
constexpr int kFallForward = 3;
constexpr int kFallBackward = 4;
constexpr int kKickEnemy = 5;
constexpr int kStompEnemy = 6;
// Reactions: scripted bolts, deaths, grabs, damage, pins, throws, floors and get-ups (7..50).
constexpr int kFirstReaction = 7;
constexpr int kLastReaction = 50;
constexpr int kPinnedFront = 39;
constexpr int kPinnedBack = 40;
constexpr int kOnFloorFront = 47;
constexpr int kOnFloorBehind = 48;
// Scripted look-arounds that turn the head (52..58) and the first scripted state the match arm skips (58).
constexpr int kFirstHeadTurn = 52;
constexpr int kRunInScene = 54;
constexpr int kFirstNoMatchArm = 58;
constexpr int kEndHeadTurn = 59;
constexpr int kHeadAndTorsoTurn = 180;
}  // namespace state

// Upper body states.
namespace upper {
constexpr int kNone = 0;
constexpr int kRunForward = 2;
constexpr int kRunRight = 6;
constexpr int kRunLeft = 7;
constexpr int kRunJumpBackward = 13;
constexpr int kAimStartTargetLock = 22;
constexpr int kAimTargetLockSwitch = 23;
constexpr int kAttack = 25;
constexpr int kReload = 26;
}  // namespace upper

// Lower body states (the aim variants are the plain ones + kAim).
namespace lower {
constexpr int kNone = 0;
constexpr int kWalkForward = 1;
constexpr int kRunForward = 2;
constexpr int kWalkBackward = 4;
constexpr int kSidestepRight = 5;
constexpr int kSidestepLeft = 6;
constexpr int kRunRight = 7;
constexpr int kRunLeft = 8;
constexpr int kQuickTurnRight = 9;
constexpr int kJumpBackward = 11;
constexpr int kAim = 20;
constexpr int kAimRunLeft = 28;
constexpr int kAttack = 34;
}  // namespace lower

// Bits of the player flags (PlayerVar::PlayerFlags) and the character flags (PlayerVar::CharaFlags).
namespace flag {
constexpr std::uint32_t kAimLockSettled = 1u << 8;
constexpr std::uint32_t kCharaStunned = 1u << 3;
}  // namespace flag

// Packed weapon attacks: weapon number + 10 * input (0 tap, 1 hold, 2 multitap); guns from here up.
constexpr int kFirstGunAttack = 32;
constexpr int kNoWeapon = -1;

// The rig: 18 bones; the upper body plays on bones 0..10, the legs on 11..17.
constexpr int kBoneCount = 18;
constexpr std::uint32_t kUpperBodyBones = 0x7FF;
constexpr std::uint32_t kLowerBodyBones = 0x3F800;
namespace bone {
constexpr int kTorso = 1;
constexpr int kHead = 2;
constexpr int kLeftUpperArm = 4;
constexpr int kRightUpperArm = 8;
constexpr int kRightForearm = 9;
constexpr int kRightHand = 10;
constexpr int kHips = 11;
}  // namespace bone

}  // namespace sh::game::harry
