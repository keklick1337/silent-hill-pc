// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/player/player_state_vars.h"

namespace sh::game {

namespace {

using V = PlayerVar;

// The field behind a value: a pointer of its width, or nothing when the value lives elsewhere.
struct Slot {
    enum class Kind : std::uint8_t { None, S8, U8, S16, U16, S32 } kind = Kind::None;
    void* p = nullptr;
};

template <class T>
Slot slot(T& field) {
    if constexpr (sizeof(T) == 1) {
        return {static_cast<T>(-1) < 0 ? Slot::Kind::S8 : Slot::Kind::U8, &field};
    } else if constexpr (sizeof(T) == 2) {
        return {static_cast<T>(-1) < 0 ? Slot::Kind::S16 : Slot::Kind::U16, &field};
    } else {
        static_assert(sizeof(T) == 4);
        return {Slot::Kind::S32, &field};
    }
}

Slot find(PlayerState& s, PlayerVar v) {
    PlayerControls& c = s.controls;
    PlayerCombatScript& k = s.combat_script;
    PlayerDamageScript& d = s.damage_script;
    switch (v) {
        case V::TurnSpeed: return slot(s.turn_speed);
        case V::StepSpeed: return slot(s.step_speed);
        case V::WallDistance: return slot(s.wall_distance);
        case V::WallGroundType: return slot(s.wall_ground_type);
        case V::HeadingOffset: return slot(s.heading_offset);
        case V::IntendedStep: return slot(s.intended_step);
        case V::PrevX: return slot(s.prev_position[0]);
        case V::PrevY: return slot(s.prev_position[1]);
        case V::PrevZ: return slot(s.prev_position[2]);
        case V::DistanceCounted: return slot(s.distance_counted);
        case V::FloorGroundType: return slot(s.floor.ground_type);
        case V::FloorHeight: return slot(s.floor.ground_height);
        case V::TargetPitch: return slot(s.target_pitch);
        case V::TargetYaw: return slot(s.target_yaw);
        case V::HitWindowStart: return slot(k.hit_window_start);
        case V::HitWindowEnd: return slot(k.hit_window_end);
        case V::MeleeClickQueue: return slot(k.melee_click_queue);
        case V::MeleeNeedsRelease: return slot(k.melee_needs_release);
        case V::PrevAltFire: return slot(k.prev_alt_fire);
        case V::FreeAimActive: return slot(k.free_aim_active);
        case V::FreeAimState: return slot(k.free_aim_state);
        case V::FreeAimStuck: return slot(k.free_aim_stuck);
        case V::FreeAimFireHeld: return slot(k.free_aim_fire_held);
        case V::FreeAimRefire: return slot(k.free_aim_refire);
        case V::FreeAimReleased: return slot(k.free_aim_released);
        case V::AimResume: return slot(k.aim_resume);
        case V::AfkFraction: return slot(k.afk_fraction);
        case V::PrevAttackX: return slot(k.prev_attack[0]);
        case V::PrevAttackY: return slot(k.prev_attack[1]);
        case V::PrevAttackZ: return slot(k.prev_attack[2]);
        case V::TargetEnemyX: return slot(s.target_enemy[0]);
        case V::TargetEnemyY: return slot(s.target_enemy[1]);
        case V::TargetEnemyZ: return slot(s.target_enemy[2]);
        case V::AltFireHeld: return slot(s.alt_fire_held);
        case V::PendingAttack: return slot(s.pending_attack);
        case V::Dead: return slot(s.dead);
        case V::DamageDisabled: return slot(s.damage_disabled);
        case V::PinTargetX: return slot(s.pin_target[0]);
        case V::PinTargetZ: return slot(s.pin_target[1]);
        case V::FlagTimerLimit: return slot(s.flag_timer_limit);
        case V::StuckState: return slot(d.stuck_state);
        case V::StuckBodyKeyframe: return slot(d.stuck_body_keyframe);
        case V::StuckUpperKeyframe: return slot(d.stuck_upper_keyframe);
        case V::StuckStill: return slot(d.stuck_still);
        case V::StuckTotal: return slot(d.stuck_total);
        case V::PrevRotY: return slot(d.prev_rot_y);
        case V::PrevRotValid: return slot(d.prev_rot_valid);
        case V::CutsceneMove: return slot(s.cutscene_move);
        case V::PathStep: return slot(s.path_step);
        case V::PushX: return slot(s.push[0]);
        case V::PushZ: return slot(s.push[1]);
        case V::ActionInput: return slot(c.has_action);
        case V::MoveInput: return slot(c.has_move);
        case V::ShootInput: return slot(c.shoot);
        case V::AttackInput: return slot(c.attack);
        case V::HoldAttackInput: return slot(c.hold_attack);
        case V::AimInput: return slot(c.aim);
        case V::RunInput: return slot(c.run);
        case V::MoveForward: return slot(c.forward);
        case V::MoveBackward: return slot(c.back);
        case V::StepLeftTap: return slot(c.step_left_tap);
        case V::StepLeftHold: return slot(c.step_left_hold);
        case V::StepRightTap: return slot(c.step_right_tap);
        case V::StepRightHold: return slot(c.step_right_hold);
        case V::TurnLeft: return slot(c.turn_left);
        case V::TurnRight: return slot(c.turn_right);
        case V::StickHold: return slot(s.stick_hold);
        case V::WalkRunBlend: return slot(s.walk_run_blend);
        case V::WeaponClip: return slot(s.weapon_clip);
        case V::RockDrillDirection: return slot(s.rock_drill_direction);
        case V::LockedTarget: return slot(s.locked_target);
        case V::MeleeAttackType: return slot(s.melee_attack_type);
        case V::Multitap: return slot(s.multitap);
        case V::AttackClip: return slot(s.attack_clip);
        case V::ReloadStartKeyframe: return slot(s.reload_start_keyframe);
        case V::ReloadEndKeyframe: return slot(s.reload_end_keyframe);
        case V::ShotYaw: return slot(s.shot_yaw);
        case V::ShotPitch: return slot(s.shot_pitch);
        case V::FlexX: return slot(s.flex_x);
        case V::FlexRotY: return slot(s.flex_y);
        case V::LastWeapon: return slot(s.last_weapon);
        case V::GrabReleaseTimer: return slot(s.grab_release_timer);
        case V::CutsceneStop: return slot(s.cutscene_stop);
        case V::ControlDisabled: return slot(s.control_disabled);
        // The equip block is read-only by name (update_weapon fills it).
        case V::WeaponAnimAimStop:
        case V::WeaponAnimAttack:
        case V::WeaponAnimAttackHold:
        case V::WeaponClipIndex:
        case V::WeaponReloadSoundKeyframe:
        default: return {};
    }
}

}  // namespace

bool player_state_get(const PlayerState& state, PlayerVar v, std::int64_t* out) {
    const EquippedWeapon& w = state.weapon;
    switch (v) {
        case V::WeaponAnimAimStop: *out = w.clip_stop_aiming; return true;
        case V::WeaponAnimAttack: *out = w.clip_attack; return true;
        case V::WeaponAnimAttackHold: *out = w.clip_attack_hold; return true;
        case V::WeaponClipIndex: *out = w.clip_index; return true;
        case V::WeaponReloadSoundKeyframe: *out = w.reload_sound_keyframe; return true;
        case V::PoweredAimKeyframe: *out = state.powered_aim_keyframe; return true;
        default: break;
    }
    const Slot s = find(const_cast<PlayerState&>(state), v);
    switch (s.kind) {
        case Slot::Kind::S8: *out = *static_cast<const std::int8_t*>(s.p); return true;
        case Slot::Kind::U8: *out = *static_cast<const std::uint8_t*>(s.p); return true;
        case Slot::Kind::S16: *out = *static_cast<const std::int16_t*>(s.p); return true;
        case Slot::Kind::U16: *out = *static_cast<const std::uint16_t*>(s.p); return true;
        case Slot::Kind::S32: *out = *static_cast<const std::int32_t*>(s.p); return true;
        case Slot::Kind::None: break;
    }
    return false;
}

bool player_state_owns(PlayerVar v) {
    PlayerState probe;
    std::int64_t ignored = 0;
    return player_state_get(probe, v, &ignored);
}

bool player_state_set(PlayerState& state, PlayerVar v, std::int64_t value) {
    const Slot s = find(state, v);
    switch (s.kind) {
        case Slot::Kind::S8: *static_cast<std::int8_t*>(s.p) = static_cast<std::int8_t>(value); return true;
        case Slot::Kind::U8: *static_cast<std::uint8_t*>(s.p) = static_cast<std::uint8_t>(value); return true;
        case Slot::Kind::S16: *static_cast<std::int16_t*>(s.p) = static_cast<std::int16_t>(value); return true;
        case Slot::Kind::U16: *static_cast<std::uint16_t*>(s.p) = static_cast<std::uint16_t>(value); return true;
        case Slot::Kind::S32: *static_cast<std::int32_t*>(s.p) = static_cast<std::int32_t>(value); return true;
        case Slot::Kind::None: break;
    }
    // The equip block is not written by name; it still belongs here (the record must not see it).
    return player_state_owns(v);
}

}  // namespace sh::game
