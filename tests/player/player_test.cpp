// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include <cstdio>

#include "engine/player/harry_states.h"
#include "engine/player/player.h"
#include "engine/player/player_math.h"
#include "engine/world/fixed_math.h"
#include "player/fake_record.h"

using sh::game::Player;
using sh::game::PlayerVar;
using V = PlayerVar;

SH_TEST(player_reset_state_rests_everything) {
    sh::test::FakePlayerRecord r;
    for (auto& v : r.vars) {
        v = 77;
    }
    r.at(V::CharaFlags) = 0xFF;
    r.at(V::PositionX) = 10;
    r.at(V::PositionY) = -20;
    r.at(V::PositionZ) = 30;
    Player p(r);
    p.reset_state();
    SH_CHECK(r.at(V::State) == 0 && r.at(V::UpperBody) == 0 && r.at(V::LowerBody) == 0);
    SH_CHECK(r.at(V::PendingAttack) == -1);
    SH_CHECK(r.at(V::AngleToTarget) == 1024);
    SH_CHECK(r.at(V::CharaFlags) == 0xF7);
    SH_CHECK(r.at(V::PrevX) == 10 && r.at(V::PrevY) == -20 && r.at(V::PrevZ) == 30);
    SH_CHECK(r.at(V::GrabberFront) == -1 && r.at(V::GrabberUnused3) == -1);
    SH_CHECK(r.at(V::CylinderHeight) == 942);
    SH_CHECK(r.at(V::TurnLeft) == 0 && r.at(V::HoldAttackInput) == 0 && r.at(V::WalkRunBlend) == 0);
    // Not touched by the reset.
    SH_CHECK(r.at(V::Exhaustion) == 77 && r.at(V::Health) == 77);
    SH_CHECK(r.log.size() == 2 && r.log[0] == "held_model_reset" && r.log[1] == "damage_clear");
}

SH_TEST(player_busy_and_attacking) {
    sh::test::FakePlayerRecord r;
    Player p(r);
    r.at(V::Health) = 4096;
    SH_CHECK(!p.busy() && !p.attacking());
    r.at(V::State) = sh::game::harry::state::kKickEnemy;
    SH_CHECK(p.busy() && p.attacking());
    r.at(V::State) = sh::game::harry::state::kLastReaction;
    SH_CHECK(p.busy() && !p.attacking());
    r.at(V::State) = sh::game::harry::state::kLastReaction + 1;
    SH_CHECK(!p.busy());
    r.at(V::UpperBody) = sh::game::harry::upper::kAttack;
    SH_CHECK(p.attacking());
    r.at(V::Health) = 0;
    SH_CHECK(p.busy());
}

SH_TEST(player_init_from_save_equips_gun_with_ammo) {
    sh::test::FakePlayerRecord r;
    r.equipped = 160;  // a gun: attack 32
    r.slots[0] = {5, 1};
    r.slots[3] = {160, 12};
    r.slots[6] = {192 & 0xFF, 40};  // its ammo (160 + 32 = 192)
    r.slot_count = 50;
    r.at(V::Difficulty) = 1;
    r.at(V::Health) = 0;
    Player p(r);
    p.init_from_save();
    SH_CHECK(r.at(V::WeaponAttack) == 32);
    SH_CHECK(r.at(V::Ammo) == 12 && r.at(V::WeaponSlot) == 3 && r.at(V::AmmoReserve) == 40);
    SH_CHECK(r.at(V::FlagTimerLimit) == 7372);
    SH_CHECK(r.slot_count == 40 && r.at(V::Health) == 1);
    SH_CHECK(r.cursor_item == 160 && r.at(V::LastWeapon) == -1 && r.at(V::MapAnimIndex) == -1);
    SH_CHECK(r.at(V::BodyAnimFlags) == 3 && r.at(V::UpperAnimFlags) == 3 && r.at(V::CollisionState) == 3);
}

SH_TEST(player_init_from_save_unarmed) {
    sh::test::FakePlayerRecord r;
    r.equipped = 0;
    r.at(V::Difficulty) = 5;   // unknown: the limit stays
    r.at(V::FlagTimerLimit) = 123;
    r.at(V::Health) = 1 << 30;
    Player p(r);
    p.init_from_save();
    SH_CHECK(r.at(V::WeaponAttack) == -1 && r.at(V::WeaponSlot) == -1 && r.at(V::Ammo) == 0);
    SH_CHECK(r.at(V::FlagTimerLimit) == 123 && r.at(V::Health) == r.max_hp);
}

SH_TEST(player_update_weapon_changes_once) {
    sh::test::FakePlayerRecord r;
    r.starts[62] = 600;
    r.ends[63] = 620;
    r.at(V::WeaponAttack) = 32;
    r.at(V::LastWeapon) = -1;
    Player p(r);
    p.update_weapon();
    SH_CHECK(r.at(V::TargetNpc) == -1);
    SH_CHECK(r.at(V::ReloadStartKeyframe) == 600 && r.at(V::ReloadEndKeyframe) == 620);
    SH_CHECK(r.log.size() == 3 && r.log[0] == "rig 32" && r.log[1] == "sound 166" && r.log[2] == "weapon_anim 32");
    r.log.clear();
    p.update_weapon();
    SH_CHECK(r.log.size() == 1);
    r.at(V::WeaponAttack) = -1;
    p.update_weapon();
    SH_CHECK(r.log.back() == "weapon_info_clear");
}

SH_TEST(player_enter_map_resets_after_clips) {
    sh::test::FakePlayerRecord r;
    r.at(V::MatchAnim) = 1;
    Player p(r);
    p.enter_map(false);
    SH_CHECK(r.at(V::MatchAnim) == 1);
    SH_CHECK(r.log.front() == "map_clips 76 0 -1");
    SH_CHECK(r.at(V::BoxTop) == -6553 && r.at(V::BoxOffsetY) == -4505 && r.at(V::CylinderRadius) == 1228);
    p.enter_map(true);
    SH_CHECK(r.at(V::MatchAnim) == 0);
}

SH_TEST(player_var_names) {
    const sh::game::PlayerVarName* v = sh::game::player_var_find("lower_body");
    SH_CHECK(v != nullptr && v->var == V::LowerBody && v->writable);
    v = sh::game::player_var_find("intended_step");
    SH_CHECK(v != nullptr && !v->writable);
    SH_CHECK(sh::game::player_var_find("nope") == nullptr);
}

SH_TEST(player_integrate_moves_and_counts) {
    sh::test::FakePlayerRecord r;
    r.at(V::DeltaTime) = 136;
    r.at(V::MoveSpeed) = 4096;  // 1 m/s straight ahead (+Z)
    r.at(V::Heading) = 0;
    r.ground = {0, 1, 0, 0};
    r.move.ground = {0, 1, 0, 0};
    r.at(V::RunInput) = 1;
    Player p(r);
    p.integrate();
    SH_CHECK(r.at(V::PositionZ) == 136 && r.at(V::PositionX) == 0);
    SH_CHECK(r.at(V::IntendedStep) == 136);
    SH_CHECK(r.at(V::RunDistance) == sh::world::planar_length(0, 136) && r.at(V::WalkDistance) == 0);
    SH_CHECK(r.root[2] == (136 >> 4));
    // A ledge 1 m below: falls forward (travelling along the facing), the aim drops.
    r.move.ground = {4096, 1, 0, 0};
    r.ground = {4096, 1, 0, 0};
    r.at(V::GroundHeight) = 4096;
    r.at(V::Aiming) = 1;
    p.integrate();
    SH_CHECK(r.at(V::State) == sh::game::harry::state::kFallForward && r.at(V::Aiming) == 0);
}

SH_TEST(player_animate_plays_both_halves_and_ends_blend) {
    sh::test::FakePlayerRecord r;
    r.at(V::DeltaTime) = 136;
    r.at(V::WeaponAttack) = -1;
    r.at(V::BodyAnimStatus) = 7;
    r.at(V::UpperAnimStatus) = 9;
    Player p(r);
    p.animate();
    SH_CHECK(r.log.size() == 3 && r.log[0] == "held 1,1" && r.log[1] == "play body 7" && r.log[2] == "play upper 9");
    SH_CHECK(r.at(V::DisabledBones) == sh::game::harry::kLowerBodyBones);
    // The walk/run hand-over plays Still on the legs and is cut after half a second without a link.
    r.at(V::WalkRunBlend) = 1;
    r.links[0] = 99;
    for (int i = 0; i < 16; ++i) {
        p.animate();
    }
    SH_CHECK(r.at(V::WalkRunBlend) == 0 && r.at(V::BodyAnimStatus) == 0);
}

SH_TEST(player_lean_classic_relaxes_and_holds_match) {
    sh::test::FakePlayerRecord r;
    r.at(V::DeltaTime) = 136;
    r.at(V::State) = sh::game::harry::state::kNone;
    r.at(V::FlexX) = 100;
    r.at(V::FlexRotY) = -50;
    r.at(V::MatchAnim) = 1;
    Player p(r);
    p.lean_classic();
    SH_CHECK(r.at(V::FlexX) == 100 - 32 - 24 && r.at(V::FlexRotY) == -50 + 24);
    SH_CHECK(r.log.size() == 6 && r.log[3] == "rot 8 0 720 -100");
}

SH_TEST(player_clip_duration_pins_by_map) {
    sh::test::FakePlayerRecord r;
    Player p(r);
    r.at(V::StickY) = -127;
    // The walk at full stick: 63 of deflection past the threshold.
    const int over = sh::game::player_math::stick_deflection(0, -127, false) - 64;
    SH_CHECK(p.clip_duration(5, 10) == (over * 2662 / 64) * 16 + 49152);
    SH_CHECK(p.clip_duration(11, 10) == 0);
    r.at(V::State) = sh::game::harry::state::kPinnedFront;
    r.rules.pin_reactions = true;  // map2_s00: its reaction rows carry the pin
    SH_CHECK(p.clip_duration(5, 10) == 61440);
    r.rules.pin_reactions = false;
    SH_CHECK(p.clip_duration(5, 10) == 0);
}

SH_TEST(player_behave_routes_states) {
    sh::test::FakePlayerRecord r;
    r.at(V::DeltaTime) = 136;
    r.at(V::MovementOriginal) = 1;
    Player p(r);
    // Free movement on the original machine: controller, legs, upper body, bookkeeping.
    p.behave();
    SH_CHECK((r.log == std::vector<std::string>{"timer", "rest_before", "controller", "legs", "upper", "rest_after"}));
    // A reaction state that takes the tick skips the bookkeeping after it.
    r.log.clear();
    r.at(V::State) = 20;
    r.state_taken = true;
    p.behave();
    SH_CHECK((r.log == std::vector<std::string>{"timer", "rest_before", "controller", "rest_state"}));
    // The idle state handled by the rest code.
    r.log.clear();
    r.at(V::State) = sh::game::harry::state::kIdle;
    r.idle_taken = true;
    r.rules.player_updates = {"padlock_update"};  // map2_s00
    p.behave();
    SH_CHECK((r.log == std::vector<std::string>{"timer", "rest_before", "map_tick", "controller", "rest_idle",
                                                "rest_after"}));
}

SH_TEST(player_alt_camera_strafe_diagonal_and_run) {
    sh::test::FakePlayerRecord r;
    r.at(V::DeltaTime) = 136;
    r.at(V::AltCamera) = 1;
    r.at(V::OrbitYaw) = 1000;
    r.at(V::RotationY) = 1000;  // the body already faces the camera
    r.at(V::WeaponAttack) = -1;
    r.at(V::BindRun) = 0x2000;
    r.at(V::PadHeld) = 0x2000;
    r.at(V::AnalogLeftX) = 128;
    r.at(V::AnalogLeftY) = 128;
    r.keys = 1 | 8;  // forward and right
    Player p(r);
    p.behave();
    SH_CHECK(r.at(V::RotationY) == 1000);
    SH_CHECK(r.at(V::HeadingOffset) == 512 && r.at(V::LegsHeading) == 512);
    SH_CHECK(r.at(V::RunInput) == 1 && r.at(V::StepSpeed) == 18432);
    SH_CHECK(r.at(V::BodyAnimStatus) == Player::status(3, false));
    SH_CHECK(r.at(V::LowerBody) == sh::game::harry::lower::kRunForward);
}

SH_TEST(player_third_person_body_eases_to_the_camera) {
    sh::test::FakePlayerRecord r;
    r.at(V::DeltaTime) = 136;  // one 30 Hz tick
    r.at(V::AltCamera) = 1;
    r.at(V::OrbitYaw) = 1024;  // the camera a quarter turn to the right of the body
    r.at(V::WeaponAttack) = -1;
    r.at(V::AnalogLeftX) = 128;
    r.at(V::AnalogLeftY) = 128;
    r.keys = 1;  // forward
    Player p(r);
    p.behave();
    // A far gap closes at the top rate (12 degrees a tick), never at once; travel is along the camera already.
    SH_CHECK(r.at(V::RotationY) == 136);
    SH_CHECK(r.at(V::HeadingOffset) == 1024 - 136);
    std::int64_t last = 136;
    bool monotonic = true;
    for (int i = 0; i < 60; ++i) {
        p.behave();
        const std::int64_t now = r.at(V::RotationY);
        monotonic = monotonic && now >= last && now <= 1024;
        last = now;
    }
    SH_CHECK(monotonic);  // no overshoot, no swing back
    SH_CHECK(r.at(V::RotationY) == 1024 && r.at(V::HeadingOffset) == 0);

    // Standing still he turns to the camera too, with no heading offset left behind; the short way round.
    r.keys = 0;
    r.at(V::OrbitYaw) = 4000;
    p.behave();
    SH_CHECK(r.at(V::RotationY) == 1024 - 136 && r.at(V::HeadingOffset) == 0);
}

SH_TEST(player_first_person_idle_look_drags_the_body) {
    sh::test::FakePlayerRecord r;
    r.at(V::DeltaTime) = 136;
    r.at(V::AltCamera) = 1;
    r.at(V::FpsCamera) = 1;
    r.at(V::WeaponAttack) = -1;
    r.at(V::AnalogLeftX) = 128;
    r.at(V::AnalogLeftY) = 128;
    Player p(r);
    r.at(V::OrbitYaw) = 400;  // a glance: the body stays
    p.behave();
    SH_CHECK(r.at(V::RotationY) == 0);
    r.at(V::OrbitYaw) = 900;  // past 45 degrees the body comes along, so a turn in place never stops
    p.behave();
    SH_CHECK(r.at(V::RotationY) == 900 - 512);
    r.keys = 1;  // walking: the body is the view
    p.behave();
    SH_CHECK(r.at(V::RotationY) == 900);
}

SH_TEST(player_control_2d_travels_along_the_screen) {
    sh::test::FakePlayerRecord r;
    r.at(V::DeltaTime) = 136;
    r.at(V::Control2d) = 1;
    r.at(V::Control2dSnap) = 1;
    r.at(V::WeaponAttack) = -1;
    r.at(V::AnalogLeftX) = 128;
    r.at(V::AnalogLeftY) = 128;
    r.cam_yaw = 1024;  // the camera looks along +X
    r.keys = 8;        // right: travel along -Z on screen right
    Player p(r);
    p.behave();
    SH_CHECK(r.at(V::MoveForward) == 1 && r.at(V::MoveInput) == 1);
    SH_CHECK(r.at(V::RotationY) == 2048 && r.at(V::HeadingOffset) == 0);
    SH_CHECK(r.at(V::StepSpeed) == 6144);
}

