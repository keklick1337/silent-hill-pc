// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The player's live state outside his character record: the controls of the tick,
// the turn and step speeds the move applies, the equipped weapon's equip block and clip keyframes, the result of the
// tick's move through the walls, the aim target, the scene and pin state, and the bookkeeping of Harry's Lua
// (gamedata/chara/Harry). sh::game::Session holds the one instance (session().player); Lua reaches every value by
// name (sh.entity.harry / set_harry, PlayerVar).
//
// Each value keeps the width of the original's variable, so a write truncates the same way. The wall result
// has the shape of the map overlays' own wall record (HarryMap_WallBlock hands either one to the wall test).

#pragma once

#include <cstdint>

namespace sh::game {

// The controls of this tick (the controller writes them, the legs and the upper body read them). Each 16-bit one is a
// short history of its input (non-zero while it counts as pressed); the turns are how far the turn is pushed (64 full).
struct PlayerControls {
    std::uint16_t forward = 0, back = 0;
    std::uint16_t turn_left = 0, turn_right = 0;
    std::uint16_t step_left_tap = 0, step_right_tap = 0;
    std::uint16_t step_left_hold = 0, step_right_hold = 0;
    std::uint16_t run = 0, aim = 0, attack = 0, shoot = 0, hold_attack = 0;
    std::int32_t has_move = 0, has_action = 0;
};

// One of the equipped weapon's ten clip keyframe records.
struct WeaponKeyframes {
    // The rig's two clip statuses and its value (gamedata/weapons rig keyframes).
    std::uint8_t status_a = 0, status_b = 0;
    std::uint8_t value = 0;
    std::int16_t start = 0;  // first keyframe of the clip
    std::int16_t ready = 0;  // the keyframe it is ready at (aim up, swing done)
};

// The equip block of the weapon in hand (gamedata/weapons rig).
struct EquippedWeapon {
    std::int16_t attack_sound = 0, reload_sound = 0, empty_sound = 0;
    std::uint8_t clip_stop_aiming = 0, clip_attack = 0, clip_attack_hold = 0;  // packed clip statuses
    std::uint8_t reload_sound_keyframe = 0;
    std::uint8_t clip_index = 0;
};

// The result of the tick's move through the walls: the offset travelled, the ground reached and the ceiling.
struct WallResult {
    std::int32_t offset[3] = {};
    std::int32_t ground_height = 0;
    std::int16_t tilt_x = 0, tilt_z = 0;
    std::int8_t ground_type = 0;
    std::uint8_t reserved[3] = {};
    std::int32_t ceiling = 0;
};

// What Harry's combat script keeps between ticks (gamedata/chara/Harry/combat.lua).
struct PlayerCombatScript {
    std::int32_t hit_window_start = 0, hit_window_end = 0;  // keyframes of the attack clip where the hit lands
    std::int32_t melee_click_queue = 0;                     // attack presses waiting for the next melee follow-up
    std::int32_t melee_needs_release = 0;                   // the next swing needs a fresh press
    std::int32_t prev_alt_fire = 0;                         // the alternate cameras' fire button last tick
    std::int32_t free_aim_active = 0;                       // the alternate cameras' gun machine owned the last tick
    std::int32_t free_aim_state = 0;                        // 0 aim, 1 fire, 2 reload
    std::int32_t free_aim_stuck = 0;                        // ticks in the state (a safety cap)
    std::int32_t free_aim_fire_held = 0;
    std::int32_t free_aim_refire = 0;                       // time until the next shot may fire (Q12 s)
    std::int32_t free_aim_released = 4096;                  // how long fire has been let go (Q12 s)
    std::int32_t aim_resume = 0;                            // time left for the instant raise after the inventory (Q12 s)
    std::int32_t afk_fraction = 0;                          // the idle timer's fraction of a 30 fps tick
    std::int32_t prev_attack[3] = {};                       // the weapon's position last tick (the guns' ray start)
};

// What Harry's damage script keeps between ticks (gamedata/chara/Harry/damage.lua): the stuck net and the facing.
struct PlayerDamageScript {
    std::int32_t stuck_state = -1;  // the reaction state the stuck net last saw (-1 none)
    std::int32_t stuck_body_keyframe = -1;
    std::int32_t stuck_upper_keyframe = -1;
    std::int32_t stuck_still = 0;   // Q12 s the clips have stood still in that state
    std::int32_t stuck_total = 0;   // Q12 s in that state
    std::int32_t prev_rot_y = 0;    // Harry's facing at the end of the previous tick
    std::int32_t prev_rot_valid = 0;
};

struct PlayerState {
    PlayerControls controls;
    WeaponKeyframes weapon_keyframes[10];
    EquippedWeapon weapon;
    WallResult floor;

    // The move: the turn and side step speeds of the legs (Q12), the heading offset of the run, the step the last
    // move meant to make (the invisible-wall gate compares the travelled one with it), where Harry stood before it,
    // and whether this tick's distance already went into the results screen's totals.
    std::int32_t turn_speed = 0;
    std::int32_t step_speed = 0;
    std::int32_t heading_offset = 0;
    std::int32_t intended_step = 0;
    std::int32_t prev_position[3] = {};
    std::int32_t distance_counted = 0;
    // The wall ahead as the run sees it: distance and its ground type.
    std::int32_t wall_distance = 0;
    std::uint8_t wall_ground_type = 0;

    // Aim: the target's position, the up-down and side angle to it, an attack requested for the next tick.
    std::int32_t target_enemy[3] = {};
    std::int16_t target_pitch = 0;
    std::int16_t target_yaw = 0;
    std::int32_t pending_attack = 0;

    std::uint8_t dead = 0;
    std::uint8_t damage_disabled = 0;
    // Scenes: the scripted move runs, the step of a scripted path walk, the shove of the tick, the pin of a grab.
    std::uint8_t cutscene_move = 0;
    std::uint8_t path_step = 0;
    std::int32_t push[2] = {};  // x, z
    std::int32_t pin_target[2] = {};
    // The limit of the stun timer.
    std::int32_t flag_timer_limit = 0;
    // The alternate cameras' fire button (mouse or bind), published by the PC input each frame.
    std::int32_t alt_fire_held = 0;

    PlayerCombatScript combat_script;
    PlayerDamageScript damage_script;
    // The controller's 30 Hz aging gates: the forward history and the attack registers.
    std::int32_t tick30_forward = 0;
    std::int32_t tick30_attack = 0;

    // The legs: how long the stick has been held (the run start), the walk-to-run blend running.
    std::uint8_t stick_hold = 0;
    std::uint8_t walk_run_blend = 0;
    // Combat: the weapon clip record in use, the rock drill's attack direction, the locked NPC (-1 none), the
    // melee attack type, a multi-tap attack, the attack clip, the reload's keyframes, the powered aim keyframe
    // (nothing writes it), the shot's yaw and pitch, the weapon held before the last change (-1 none).
    std::uint8_t weapon_clip = 0;
    std::int8_t rock_drill_direction = 0;
    std::int32_t locked_target = -1;
    std::int32_t melee_attack_type = 0;
    std::int32_t multitap = 0;
    std::int32_t attack_clip = 0;
    std::int16_t reload_start_keyframe = 0;
    std::int16_t reload_end_keyframe = 0;
    std::int16_t powered_aim_keyframe = 0;
    std::int16_t shot_yaw = 0;
    std::int16_t shot_pitch = 0;
    std::int32_t last_weapon = -1;
    // The torso flex (Q12 angles) the pose applies; the time left before a grab lets the input go.
    std::int16_t flex_x = 0;
    std::int16_t flex_y = 0;
    std::int32_t grab_release_timer = 0;
    // Scenes: the scene's stop of the legs; the player's control taken away.
    std::uint8_t cutscene_stop = 0;
    std::uint8_t control_disabled = 0;
};

}  // namespace sh::game
