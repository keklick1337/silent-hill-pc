// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/player/player.h"

#include "engine/player/harry_states.h"

#include <algorithm>
#include <cstring>

namespace sh::game {

namespace {

using V = PlayerVar;

PlayerRecord* s_record = nullptr;

// Default collision shape (Q12 metres).
constexpr std::int64_t kCylinderRadius = 1228;   // 0.3 m
constexpr std::int64_t kCylinderHeight = 942;    // 0.23 m
constexpr std::int64_t kBoxTop = -6553;          // -1.6 m
constexpr std::int64_t kBoxOffsetY = -4505;      // -1.1 m
constexpr std::int64_t kCollisionActive = 3;
constexpr std::int64_t kAimLookAhead = 1024;     // quarter turn: the aim looks straight ahead

// Stun timer limit by difficulty (-1 easy, 0 normal, 1 hard), Q12 seconds.
std::int64_t stun_limit(std::int64_t difficulty, std::int64_t current) {
    switch (difficulty) {
        case -1: return 20480;  // 5 s
        case 0: return 10240;   // 2.5 s
        case 1: return 7372;    // 1.8 s
        default: return current;
    }
}

// Sound commands of the weapons' ready sounds (the game's sound bank), by weapon name.
struct ReadySound {
    const char* weapon;
    int command;
};
constexpr ReadySound kReadySounds[] = {
    {"KitchenKnife", 164}, {"SteelPipe", 164}, {"Hammer", 164}, {"Katana", 164}, {"Axe", 164},
    {"Chainsaw", 169},     {"RockDrill", 163}, {"Handgun", 166}, {"HuntingRifle", 167}, {"Shotgun", 168},
    {"HyperBlaster", 165},
};

constexpr int kInventorySlotsMax = 40;
constexpr int kInventorySlotsMin = 8;

}  // namespace

void bind_player_record(PlayerRecord* record) { s_record = record; }

Player& player() {
    static Player* p = nullptr;
    static PlayerRecord* bound = nullptr;
    if (p == nullptr || bound != s_record) {
        delete p;
        p = new Player(*s_record);
        bound = s_record;
    }
    return *p;
}

void Player::clear_controls() {
    for (V v : {V::ActionInput, V::MoveInput, V::ShootInput, V::AttackInput, V::HoldAttackInput, V::AimInput,
                V::RunInput, V::MoveBackward, V::MoveForward, V::StepRightTap, V::StepRightHold, V::TurnRight,
                V::StepLeftTap, V::StepLeftHold, V::TurnLeft, V::WalkRunBlend}) {
        set(v, 0);
    }
}

void Player::reset_state() {
    set(V::WalkRunBlend, 0);
    set(V::State, harry::state::kNone);
    set(V::UpperBody, harry::upper::kNone);
    set(V::LowerBody, harry::lower::kNone);
    set(V::UpperStateStep, 0);
    set(V::UpperControlState, 0);
    set(V::BodyStateStep, 0);
    set(V::BodyControlState, 0);
    set(V::StopFlags, 0);
    set(V::FlexRotY, 0);
    set(V::FlexX, 0);
    set(V::PendingAttack, -1);
    set(V::Aiming, 0);
    r_.reset_held_weapon_model();
    set(V::AngleToTarget, kAimLookAhead);
    set(V::Dead, 0);
    set(V::DamageDisabled, 0);
    for (V v : {V::ActionInput, V::MoveInput, V::ShootInput, V::AttackInput}) {
        set(v, 0);
    }
    for (V v : {V::AfkTimer, V::FlexTargetX, V::RunSteps, V::AimHold, V::AttackTime, V::RunAhead, V::FlagTimer,
                V::PlayerFlags, V::LegsMoveSpeed}) {
        set(v, 0);
    }
    r_.clear_damage();
    set(V::HoldAttackInput, 0);
    set(V::CharaFlags, get(V::CharaFlags) & ~static_cast<std::int64_t>(harry::flag::kCharaStunned));
    set(V::PrevX, get(V::PositionX));
    set(V::PrevY, get(V::PositionY));
    set(V::PrevZ, get(V::PositionZ));
    set(V::TargetNpc, -1);
    set(V::Attacker, -1);
    set(V::AttackReceived, -1);
    for (V v : {V::GrabberUnused3, V::GrabberUnused2, V::GrabberBack, V::GrabberFront}) {
        set(v, -1);
    }
    set(V::CylinderHeight, kCylinderHeight);
    for (V v : {V::AimInput, V::RunInput, V::MoveBackward, V::MoveForward, V::StepRightTap, V::StepRightHold,
                V::TurnRight, V::StepLeftTap, V::StepLeftHold, V::TurnLeft}) {
        set(v, 0);
    }
}

void Player::enter_state(int state) {
    set(V::State, state);
    set(V::BodyStateStep, 0);
    set(V::BodyControlState, 0);
    set(V::UpperStateStep, 0);
    set(V::UpperControlState, 0);
    set(V::UpperBody, harry::upper::kNone);
    set(V::LowerBody, harry::lower::kNone);
}

void Player::init_from_save() {
    r_.read_from_save();
    r_.assign_harry_models();
    set(V::CylinderRadius, kCylinderRadius);
    set(V::CylinderHeight, kCylinderHeight);
    set(V::MatchAnim, 0);
    constexpr std::int64_t kUnlockedVisible = 3;
    set(V::BodyAnimFlags, get(V::BodyAnimFlags) | kUnlockedVisible);
    set(V::UpperAnimFlags, get(V::UpperAnimFlags) | kUnlockedVisible);
    set(V::CollisionState, kCollisionActive);

    const int equipped = r_.equipped_item();
    r_.set_inventory_cursor_item(equipped);

    bool gun = false;
    const int attack = r_.weapon_attack_of_item(equipped, &gun);
    if (attack != harry::kNoWeapon) {
        // The scans stop on the item or one past the last slot, which is still read (the game's own bound).
        int slot = 0;
        while (slot < kInventorySlotsMax && r_.inventory_slot(slot).item != equipped) {
            ++slot;
        }
        set(V::WeaponAttack, attack);
        set(V::Ammo, r_.inventory_slot(slot).count);
        set(V::WeaponSlot, slot);
        if (!gun) {
            set(V::AmmoReserve, 0);
        } else {
            const int ammo = r_.ammo_item_of(equipped);
            int ammo_slot = 0;
            while (ammo_slot < kInventorySlotsMax && r_.inventory_slot(ammo_slot).item != ammo) {
                ++ammo_slot;
            }
            set(V::AmmoReserve,
                ammo_slot == kInventorySlotsMax ? 0 : r_.inventory_slot(ammo_slot).count);
        }
    } else {
        set(V::WeaponAttack, harry::kNoWeapon);
        set(V::Ammo, 0);
        set(V::AmmoReserve, 0);
        set(V::WeaponSlot, -1);
    }

    set(V::Aiming, 0);
    set(V::GrabReleaseTimer, 0);
    set(V::PathStep, 0);
    set(V::CutsceneMove, 0);
    set(V::ControlDisabled, 0);
    set(V::FlagTimerLimit, stun_limit(get(V::Difficulty), get(V::FlagTimerLimit)));
    set(V::LastWeapon, harry::kNoWeapon);
    set(V::MapAnimIndex, -1);

    r_.set_inventory_slot_count(std::clamp(r_.inventory_slot_count(), kInventorySlotsMin, kInventorySlotsMax));
    set(V::Health, std::clamp<std::int64_t>(get(V::Health), 1, r_.max_health()));
}

void Player::enter_map(bool put_out_match) {
    r_.install_map_clips(kMapClipSlot, 0, -1);
    if (put_out_match) {
        set(V::MatchAnim, 0);
    }
    set(V::Exhaustion, 0);
    set(V::BoxTop, kBoxTop);
    set(V::BoxBottom, 0);
    set(V::BoxOffsetY, kBoxOffsetY);
    set(V::CylinderOffsetZ, 0);
    set(V::CylinderOffsetX, 0);
    set(V::BoxOffsetZ, 0);
    set(V::BoxOffsetX, 0);
    set(V::CylinderRadius, kCylinderRadius);
    set(V::CylinderHeight, kCylinderHeight);
    set(V::MapAnimIndex, -1);
    reset_state();
}

void Player::start_reaction_clip(int trigger) {
    if (get(V::UpperControlState) != 0) {
        return;
    }
    const int rows = r_.reaction_row_count();
    for (int i = 0; i < rows; ++i) {
        const ReactionRow row = r_.reaction_row(i);
        if (row.trigger != trigger) {
            continue;
        }
        if (get(V::UpperStateStep) == 0) {
            set(V::UpperAnimStatus, row.status);
            set(V::UpperStateStep, get(V::UpperStateStep) + 1);
        }
        if (get(V::BodyStateStep) == 0) {
            set(V::BodyAnimStatus, row.status);
            set(V::BodyStateStep, get(V::BodyStateStep) + 1);
        }
        set(V::WeaponClip, i);
        set(V::UpperControlState, get(V::UpperControlState) + 1);
        return;
    }
}

void Player::switch_reaction_clip(int trigger) {
    if (get(V::UpperControlState) != 0) {
        return;
    }
    const int rows = r_.reaction_row_count();
    for (int i = 0; i < rows; ++i) {
        const ReactionRow row = r_.reaction_row(i);
        if (row.trigger != trigger) {
            continue;
        }
        set(V::UpperAnimStatus, row.status + 1);
        set(V::BodyAnimStatus, row.status + 1);
        set(V::UpperStateStep, get(V::UpperStateStep) + 1);
        set(V::BodyStateStep, get(V::BodyStateStep) + 1);
        set(V::UpperAnimTime, row.time * 4096);
        set(V::WeaponClip, i);
        set(V::BodyAnimTime, row.time * 4096);
        set(V::UpperControlState, get(V::UpperControlState) + 1);
        return;
    }
}

bool Player::busy() const {
    const std::int64_t s = get(V::State);
    return get(V::Health) <= 0 || get(V::Aiming) != 0 || s == harry::state::kKickEnemy ||
           s == harry::state::kStompEnemy || (s >= harry::state::kFirstReaction && s <= harry::state::kLastReaction);
}

bool Player::attacking() const {
    const std::int64_t s = get(V::State);
    return get(V::UpperBody) == harry::upper::kAttack || s == harry::state::kKickEnemy ||
           s == harry::state::kStompEnemy;
}

bool Player::gun_attack(std::int64_t weapon_attack) const {
    const int attack = static_cast<int>(weapon_attack);
    return r_.mod_attack_weapon(attack) >= 0 ? r_.mod_attack_gun(attack) : attack >= harry::kFirstGunAttack;
}

void Player::play_ready_sound(int weapon_attack) {
    // Only a tap-packed attack is a weapon's own number; hold and multitap attacks name no weapon.
    const char* name = r_.weapon_name(weapon_attack);
    if (name == nullptr) {
        return;
    }
    for (const ReadySound& s : kReadySounds) {
        if (std::strcmp(s.weapon, name) == 0) {
            r_.sound_command(s.command);
            return;
        }
    }
}

void Player::update_weapon() {
    set(V::TargetNpc, -1);

    const int attack = static_cast<int>(get(V::WeaponAttack));
    if (attack == harry::kNoWeapon) {
        r_.clear_weapon_info();
        return;
    }
    // Only a tap-packed attack (the weapon's own number) has a rig; anything else keeps the previous block.
    if (!r_.install_weapon_rig(attack)) {
        return;
    }
    if (gun_attack(attack)) {
        const int recoil = r_.clip_index("handgun_recoil");
        set(V::ReloadStartKeyframe, r_.clip_start(status(recoil, false)));
        set(V::ReloadEndKeyframe, r_.clip_end(status(recoil, true)));
    }
    if (get(V::LastWeapon) != attack) {
        set(V::LastWeapon, attack);
        play_ready_sound(attack);
        r_.load_weapon_animation(attack);
    }
}

}  // namespace sh::game
