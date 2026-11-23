// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/inventory/exit_anim.h"

namespace sh::inventory {

namespace {

constexpr std::int32_t kStateCombat = 1;
constexpr std::int32_t kStateIdle = 2;

constexpr std::int32_t kLowerQuickTurnRight = 9;
constexpr std::int32_t kLowerQuickTurnLeft = 10;
constexpr std::int32_t kLowerJumpBackward = 11;
constexpr std::int32_t kLowerAim = 20;
constexpr std::int32_t kLowerAimQuickTurnRight = 29;
constexpr std::int32_t kLowerAimQuickTurnLeft = 30;

constexpr std::int32_t kChainsaw = 5;
constexpr std::int32_t kRockDrill = 2;

// Anim index 33, inactive: the first of the aiming anims that have a non-aiming twin ten
// statuses (five anims) below.
constexpr std::uint8_t kFirstAimStatus = 33 << 1;
constexpr std::uint8_t kAimTwinOffset = 10;
constexpr std::uint8_t kTransitionToStillActive = (1 << 1) | 1;
constexpr std::int16_t kFullAlpha = 4096;

void reset_control(ExitAnimModel& model) {
    model.control_state = 0;
    model.state_step = 0;
}

}  // namespace

void exit_anim_apply(ExitAnimPlayer& p) {
    if (p.state >= 0 && p.state < kStateIdle) {
        const bool weapon_changed = p.previous_attack != p.weapon_attack;
        if (weapon_changed) {
            p.is_aiming = 0;
        }

        // A powered weapon put away loses its power. The original compares the previous weapon
        // id with the new packed attack, so a powered weapon re-equipped with another input
        // type also counts as put away.
        const std::int32_t previous_id = p.previous_attack % 10;
        if ((previous_id == kChainsaw && p.weapon_attack != previous_id) ||
            (previous_id == kRockDrill && p.weapon_attack != previous_id)) {
            p.power_effect = 0;
            p.gas_power_timer = 0;
        }

        switch (p.lower_body_state) {
            case kLowerQuickTurnRight:
            case kLowerQuickTurnLeft:
            case kLowerJumpBackward:
            case kLowerAimQuickTurnRight:
            case kLowerAimQuickTurnLeft:
                break;

            default:
                if (p.state == kStateCombat && weapon_changed) {
                    p.state = 0;
                    p.flags = 0;
                }

                if (p.lower_body_state >= kLowerAim && p.weapon_attack != -1 && !weapon_changed) {
                    // Still aiming the same weapon: drop to the non-aiming twin of the anim.
                    if (p.upper.anim_status >= kFirstAimStatus) {
                        p.upper.anim_status = static_cast<std::uint8_t>(p.upper.anim_status - kAimTwinOffset);
                        reset_control(p.body);
                        reset_control(p.upper);
                        p.body.anim_status = static_cast<std::uint8_t>(p.body.anim_status - kAimTwinOffset);
                    }
                } else {
                    p.body.anim_status = kTransitionToStillActive;
                    p.body.keyframe = 0;
                    p.upper.anim_status = kTransitionToStillActive;
                    p.upper.keyframe = 0;
                    reset_control(p.body);
                    p.upper_body_state = 0;
                    p.lower_body_state = 0;
                    reset_control(p.upper);
                }

                p.move_speed = 0;
                p.afk_timer = 0;
        }
    }

    p.body.anim_alpha = kFullAlpha;
    p.upper.anim_alpha = kFullAlpha;
    p.body.anim_time = static_cast<std::int32_t>(p.body.keyframe) * 4096;
    p.upper.anim_time = static_cast<std::int32_t>(p.upper.keyframe) * 4096;
}

}  // namespace sh::inventory
