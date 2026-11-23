// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Leaving the inventory with a weapon change: which of the player's states and animations are reset so the new weapon starts
// clean, and the frame-zero pose both body halves are posed at. The bone posing stays with the
// host. Plain fields in and out; standard library only.

#pragma once

#include <cstdint>

namespace sh::inventory {

// One half of the player's model: the body model (legs) or the upper-body model (torso, arms).
struct ExitAnimModel {
    std::uint8_t anim_status = 0;  // anim index << 1 | active
    std::int16_t keyframe = 0;
    std::uint8_t control_state = 0;
    std::uint8_t state_step = 0;
    std::int32_t anim_time = 0;    // out: the keyframe in 20.12
    std::int16_t anim_alpha = 0;   // out: 4096
};

struct ExitAnimPlayer {
    std::int32_t state = 0;             // 0 none, 1 combat, 2 idle and up
    std::int32_t upper_body_state = 0;
    std::int32_t lower_body_state = 0;
    std::int32_t flags = 0;
    std::int16_t move_speed = 0;
    std::int32_t afk_timer = 0;
    std::int32_t gas_power_timer = 0;   // the chainsaw / rock drill power
    std::int16_t power_effect = 0;      // the running-weapon attack state
    std::int32_t weapon_attack = 0;     // the packed attack (id + 10 * input type) now equipped, -1 none
    std::int32_t previous_attack = 0;   // the packed attack held before the inventory opened
    std::uint8_t is_aiming = 0;
    ExitAnimModel body;                 // the whole-body model
    ExitAnimModel upper;                // the upper-body model
};

// Applies the exit to `player` in place.
void exit_anim_apply(ExitAnimPlayer& player);

}  // namespace sh::inventory
