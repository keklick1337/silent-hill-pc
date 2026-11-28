// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/player/player_vars.h"

namespace sh::game {

namespace {

const PlayerVarName kNames[] = {
    // e_PlayerLowerBodyState / e_PlayerUpperBodyState / e_PlayerState values.
    {"lower_body", PlayerVar::LowerBody, true},
    {"upper_body", PlayerVar::UpperBody, true},
    {"state", PlayerVar::State, true},
    // The upper body's own clip and steps.
    {"upper_anim_status", PlayerVar::UpperAnimStatus, true},
    {"upper_anim_keyframe", PlayerVar::UpperAnimKeyframe, true},
    {"upper_anim_time", PlayerVar::UpperAnimTime, true},
    {"upper_state_step", PlayerVar::UpperStateStep, true},
    {"upper_control_state", PlayerVar::UpperControlState, true},
    // Bit 0: forward was let go during this walk, bit 1: back was let go.
    {"stop_flags", PlayerVar::StopFlags, true},
    {"aiming", PlayerVar::Aiming, true},
    {"weapon_attack", PlayerVar::WeaponAttack, true},
    {"target_npc", PlayerVar::TargetNpc, true},
    // The results screen's totals (unsigned Q12).
    {"walk_distance", PlayerVar::WalkDistance, true},
    {"run_distance", PlayerVar::RunDistance, true},
    // Turn this tick (Q12 angle) and the forward speed the integration applies.
    {"turn_speed", PlayerVar::TurnSpeed, true},
    {"step_speed", PlayerVar::StepSpeed, true},
    // Stick deflection (0..127) kept while the stick comes back to centre: the walk eases out.
    {"stick_hold", PlayerVar::StickHold, true},
    // The look-ahead along the heading: distance to the wall and its ground type.
    {"wall_distance", PlayerVar::WallDistance, true},
    {"wall_ground_type", PlayerVar::WallGroundType, true},
    // Direction of travel relative to the body (0 forward, 1024 right, 2048 back).
    {"heading_offset", PlayerVar::HeadingOffset, true},
    // The legs blend between the walk and the run cycles (the Still clip carries them over).
    {"walk_run_blend", PlayerVar::WalkRunBlend, true},
    // The step the last integration meant to make, before collision cut it.
    {"intended_step", PlayerVar::IntendedStep, false},
    {"prev_x", PlayerVar::PrevX, true},
    {"prev_z", PlayerVar::PrevZ, true},
    // The legs counted this tick's distance (the alternate cameras' fallback stands down).
    {"distance_counted", PlayerVar::DistanceCounted, true},
    // The floor under Harry from the last collision pass.
    {"floor_ground_type", PlayerVar::FloorGroundType, false},
    {"floor_height", PlayerVar::FloorHeight, false},
    // Index of the equipped weapon's clip in use (entity.harry_weapon_clip).
    {"weapon_clip", PlayerVar::WeaponClip, true},
    // Clip statuses of the equipped weapon.
    {"weapon_anim_aim_stop", PlayerVar::WeaponAnimAimStop, false},
    {"weapon_anim_attack", PlayerVar::WeaponAnimAttack, false},
    {"weapon_anim_attack_hold", PlayerVar::WeaponAnimAttackHold, false},
    // The rock drill's aimed attack: 1 up, -1 down, 0 level.
    {"rock_drill_direction", PlayerVar::RockDrillDirection, false},
    // Game options: walk/run reversed, weapon control press (1) or switch (0), retreat turn reversed.
    {"option_walk_run", PlayerVar::OptionWalkRun, false},
    {"option_weapon_control", PlayerVar::OptionWeaponControl, false},
    {"option_retreat_turn", PlayerVar::OptionRetreatTurn, false},
    // One of the PC's alternate cameras (third person, first person) is on.
    {"alt_camera", PlayerVar::AltCamera, false},
    // Rounds in the gun and the rounds of its ammo carried; the inventory slot of the weapon.
    {"ammo", PlayerVar::Ammo, true},
    {"ammo_reserve", PlayerVar::AmmoReserve, true},
    {"weapon_slot", PlayerVar::WeaponSlot, false},
    // Where the weapon is (the start of the swing or the shot).
    {"attack_x", PlayerVar::AttackX, true},
    {"attack_y", PlayerVar::AttackY, true},
    {"attack_z", PlayerVar::AttackZ, true},
    // The NPC index the aim keeps (-1 none) and the shot's angles to it (-1: none fixed).
    {"locked_target", PlayerVar::LockedTarget, true},
    {"target_pitch", PlayerVar::TargetPitch, true},
    {"target_yaw", PlayerVar::TargetYaw, true},
    // Keyframes of the attack clip where the hit lands.
    {"hit_window_start", PlayerVar::HitWindowStart, true},
    {"hit_window_end", PlayerVar::HitWindowEnd, true},
    // 0 tap, 1 hold, 2 multitap; a follow-up swing is queued; the attack clip's table index.
    {"melee_attack_type", PlayerVar::MeleeAttackType, true},
    {"multitap", PlayerVar::Multitap, true},
    {"attack_clip", PlayerVar::AttackClip, true},
    {"melee_click_queue", PlayerVar::MeleeClickQueue, true},
    {"melee_needs_release", PlayerVar::MeleeNeedsRelease, true},
    {"prev_alt_fire", PlayerVar::PrevAltFire, true},
    {"free_aim_active", PlayerVar::FreeAimActive, true},
    {"free_aim_state", PlayerVar::FreeAimState, true},
    {"free_aim_stuck", PlayerVar::FreeAimStuck, true},
    {"free_aim_fire_held", PlayerVar::FreeAimFireHeld, true},
    {"free_aim_refire", PlayerVar::FreeAimRefire, true},
    {"free_aim_released", PlayerVar::FreeAimReleased, true},
    {"aim_resume", PlayerVar::AimResume, true},
    {"afk_fraction", PlayerVar::AfkFraction, true},
    {"prev_attack_x", PlayerVar::PrevAttackX, true},
    {"prev_attack_y", PlayerVar::PrevAttackY, true},
    {"prev_attack_z", PlayerVar::PrevAttackZ, true},
    // Where the locked target stood when the aim last looked.
    {"target_enemy_x", PlayerVar::TargetEnemyX, true},
    {"target_enemy_y", PlayerVar::TargetEnemyY, true},
    {"target_enemy_z", PlayerVar::TargetEnemyZ, true},
    {"prev_y", PlayerVar::PrevY, false},
    // The PC's reload button was pressed (a short latch).
    {"reload_request", PlayerVar::ReloadRequest, true},
    // The equipped weapon's clips: the reload's first and last keyframes, the powered weapon's ready pose,
    // the clip numbers (two nibbles), the reload sound's keyframe after the reload start.
    {"reload_start_keyframe", PlayerVar::ReloadStartKeyframe, false},
    {"reload_end_keyframe", PlayerVar::ReloadEndKeyframe, false},
    {"powered_aim_keyframe", PlayerVar::PoweredAimKeyframe, false},
    {"weapon_clip_index", PlayerVar::WeaponClipIndex, false},
    {"weapon_reload_sound_keyframe", PlayerVar::WeaponReloadSoundKeyframe, false},
    // The last attack's angles (other code reads them).
    {"shot_yaw", PlayerVar::ShotYaw, true},
    {"shot_pitch", PlayerVar::ShotPitch, true},
    // Ticks of verbose tracing after a shot (the main loop counts it down).
    {"post_fire_trace", PlayerVar::PostFireTrace, true},
    {"flex_x", PlayerVar::FlexX, true},
    {"timer_2c", PlayerVar::Timer2C, true},
    // Game options and PC settings.
    {"option_auto_aim", PlayerVar::OptionAutoAim, false},
    {"fps_camera", PlayerVar::FpsCamera, false},
    {"control_2d", PlayerVar::Control2d, false},
    {"demo", PlayerVar::Demo, false},
    {"infinite_ammo", PlayerVar::InfiniteAmmo, false},
    {"auto_reload", PlayerVar::AutoReload, false},
    {"option_auto_reload", PlayerVar::OptionAutoReload, false},
    {"aim_assist", PlayerVar::AimAssist, false},
    {"alt_fire_held", PlayerVar::AltFireHeld, false},
    // The equipped item is a gun.
    {"equipped_gun", PlayerVar::EquippedGun, false},
    // The weapon number of the equipped item (its item number less the first weapon's).
    {"equipped_weapon", PlayerVar::EquippedWeapon, false},
    // The attack that landed while the legs changed gait: its reaction waits for the next tick (-1 none).
    {"pending_attack", PlayerVar::PendingAttack, true},
    // Health ran out: Harry is dying; damage is off (cutscenes, the death itself); god mode (console).
    {"dead", PlayerVar::Dead, true},
    {"damage_disabled", PlayerVar::DamageDisabled, false},
    {"god_mode", PlayerVar::GodMode, false},
    // The grab's break-free meter (presses, Q12-scaled) against the count of the difficulty.
    {"grab_release_timer", PlayerVar::GrabReleaseTimer, true},
    // Where the pinning monster holds Harry (x, z).
    {"pin_target_x", PlayerVar::PinTargetX, true},
    {"pin_target_z", PlayerVar::PinTargetZ, true},
    // The stun timer's limit (Q12 s).
    {"flag_timer_limit", PlayerVar::FlagTimerLimit, false},
    // Health still to come back from an ampoule (Q12).
    {"saturation", PlayerVar::Saturation, true},
    // -1 easy, 0 normal, 1 hard; the map's number (0 the prologue).
    {"difficulty", PlayerVar::Difficulty, false},
    {"map_index", PlayerVar::MapIndex, false},
    // Nothing is equipped.
    {"unequipped", PlayerVar::Unequipped, true},
    // Game indices of the NPCs holding Harry (front, back; the other two are cleared with them).
    {"grabber_front", PlayerVar::GrabberFront, true},
    {"grabber_back", PlayerVar::GrabberBack, true},
    {"grabber_unused_2", PlayerVar::GrabberUnused2, true},
    {"grabber_unused_3", PlayerVar::GrabberUnused3, true},
    // Animation flags of the upper body (bit 0 unlocked, bit 1 visible).
    {"upper_anim_flags", PlayerVar::UpperAnimFlags, true},
    // The PC's rear-look button is held.
    {"rear_look", PlayerVar::RearLook, false},
    {"stuck_state", PlayerVar::StuckState, true},
    {"stuck_body_keyframe", PlayerVar::StuckBodyKeyframe, true},
    {"stuck_upper_keyframe", PlayerVar::StuckUpperKeyframe, true},
    {"stuck_still", PlayerVar::StuckStill, true},
    {"stuck_total", PlayerVar::StuckTotal, true},
    // Harry's facing at the end of the previous tick (the camera's chase reads how far he turned).
    {"prev_rot_y", PlayerVar::PrevRotY, true},
    {"prev_rot_valid", PlayerVar::PrevRotValid, true},
    // The cutscene's move request while Harry has no control (0 run, 1 walk, 3 turn right, 4 turn left)
    // and its stop request (Harry/cutscene.lua).
    {"cutscene_move", PlayerVar::CutsceneMove, true},
    {"cutscene_stop", PlayerVar::CutsceneStop, true},
    // The upper body's lean into a turn on the spot (Q12 angle).
    {"flex_rot_y", PlayerVar::FlexRotY, true},
    // The match of the first scene has been struck (its arm clip runs).
    {"match_anim", PlayerVar::MatchAnim, true},
    // The player has no control of Harry (scenes and events run him; Harry/scripted.lua).
    {"control_disabled", PlayerVar::ControlDisabled, true},
    // Step of the scripted walk along points (0 idle).
    {"path_step", PlayerVar::PathStep, true},
    {"upper_anim_alpha", PlayerVar::UpperAnimAlpha, true},
    // Where a pushing monster (the Split Head) shoves Harry this tick (x, z; 0, 0 none).
    {"push_x", PlayerVar::PushX, true},
    {"push_z", PlayerVar::PushZ, true},
};

}  // namespace

const PlayerVarName* player_var_find(std::string_view name) {
    for (const PlayerVarName& v : kNames) {
        if (name == v.name) {
            return &v;
        }
    }
    return nullptr;
}

}  // namespace sh::game
