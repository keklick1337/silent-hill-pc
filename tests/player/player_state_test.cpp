// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include "engine/player/player_state_vars.h"

using sh::game::PlayerState;
using sh::game::PlayerVar;

SH_TEST(player_state_starts_like_the_game) {
    const PlayerState s;
    SH_CHECK(s.damage_script.stuck_state == -1 && s.damage_script.stuck_body_keyframe == -1);
    SH_CHECK(s.combat_script.free_aim_released == 4096);
    SH_CHECK(s.controls.forward == 0 && s.dead == 0);
}

SH_TEST(player_state_values_store_like_their_fields) {
    PlayerState s;
    std::int64_t v = 0;
    // 16-bit controls keep the low bits, unsigned.
    SH_CHECK(sh::game::player_state_set(s, PlayerVar::MoveForward, 0x12345));
    SH_CHECK(sh::game::player_state_get(s, PlayerVar::MoveForward, &v) && v == 0x2345);
    // Signed bytes and shorts sign-extend.
    SH_CHECK(sh::game::player_state_set(s, PlayerVar::RockDrillDirection, 0xFF));
    SH_CHECK(sh::game::player_state_get(s, PlayerVar::RockDrillDirection, &v) && v == -1);
    // A packed attack is no byte: a mod weapon's row past 127 stays whole.
    SH_CHECK(sh::game::player_state_set(s, PlayerVar::PendingAttack, 140));
    SH_CHECK(sh::game::player_state_get(s, PlayerVar::PendingAttack, &v) && v == 140);
    SH_CHECK(sh::game::player_state_set(s, PlayerVar::TargetYaw, 0x18000));
    SH_CHECK(sh::game::player_state_get(s, PlayerVar::TargetYaw, &v) && v == -32768);
    // The floor result is the wall result block.
    SH_CHECK(sh::game::player_state_set(s, PlayerVar::FloorHeight, -40960));
    SH_CHECK(s.floor.ground_height == -40960);
    SH_CHECK(sh::game::player_state_set(s, PlayerVar::StuckTotal, 77) && s.damage_script.stuck_total == 77);
    SH_CHECK(sh::game::player_state_set(s, PlayerVar::PrevAttackY, 9) && s.combat_script.prev_attack[1] == 9);
}

SH_TEST(player_state_equip_block_is_read_only_by_name) {
    PlayerState s;
    s.weapon.clip_attack = 12;
    std::int64_t v = 0;
    SH_CHECK(sh::game::player_state_get(s, PlayerVar::WeaponAnimAttack, &v) && v == 12);
    SH_CHECK(sh::game::player_state_set(s, PlayerVar::WeaponAnimAttack, 99));
    SH_CHECK(s.weapon.clip_attack == 12);
}

// The state of the legs, combat and scenes: the game's start values and widths.
SH_TEST(player_state_keeps_the_player_globals) {
    PlayerState s;
    std::int64_t v = 0;
    SH_CHECK(sh::game::player_state_get(s, PlayerVar::LockedTarget, &v) && v == -1);
    SH_CHECK(sh::game::player_state_get(s, PlayerVar::LastWeapon, &v) && v == -1);
    SH_CHECK(sh::game::player_state_set(s, PlayerVar::StickHold, 0x1FF) && s.stick_hold == 0xFF);
    SH_CHECK(sh::game::player_state_set(s, PlayerVar::RockDrillDirection, 0xFF));
    SH_CHECK(sh::game::player_state_get(s, PlayerVar::RockDrillDirection, &v) && v == -1);
    SH_CHECK(sh::game::player_state_set(s, PlayerVar::FlexRotY, 0x10010) && s.flex_y == 0x10);
    SH_CHECK(sh::game::player_state_set(s, PlayerVar::ControlDisabled, 1) && s.control_disabled == 1);
    // Nothing writes the powered aim keyframe: a write by name is dropped.
    SH_CHECK(sh::game::player_state_set(s, PlayerVar::PoweredAimKeyframe, 5));
    SH_CHECK(sh::game::player_state_get(s, PlayerVar::PoweredAimKeyframe, &v) && v == 0);
}

SH_TEST(player_state_leaves_record_values_alone) {
    PlayerState s;
    std::int64_t v = 0;
    SH_CHECK(!sh::game::player_state_get(s, PlayerVar::Health, &v));
    SH_CHECK(!sh::game::player_state_set(s, PlayerVar::PositionX, 1));
    SH_CHECK(!sh::game::player_state_owns(PlayerVar::LowerBody));
    SH_CHECK(sh::game::player_state_owns(PlayerVar::TurnSpeed));
}
