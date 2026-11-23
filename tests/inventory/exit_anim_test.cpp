// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include "engine/inventory/exit_anim.h"

using sh::inventory::ExitAnimPlayer;
using sh::inventory::exit_anim_apply;

namespace {

ExitAnimPlayer sample() {
    ExitAnimPlayer p;
    p.state = 1;
    p.upper_body_state = 4;
    p.lower_body_state = 1;
    p.flags = 0x55;
    p.move_speed = 300;
    p.afk_timer = 9000;
    p.gas_power_timer = 4000;
    p.power_effect = 7;
    p.weapon_attack = 5;
    p.previous_attack = 5;
    p.is_aiming = 1;
    p.body = {80, 12, 3, 1, 0, 0};
    p.upper = {70, 9, 4, 1, 0, 0};
    return p;
}

}  // namespace

SH_TEST(exit_anim_same_weapon_walking_goes_still) {
    ExitAnimPlayer p = sample();
    exit_anim_apply(p);
    SH_CHECK(p.state == 1 && p.flags == 0x55 && p.is_aiming == 1);
    SH_CHECK(p.gas_power_timer == 4000 && p.power_effect == 7);
    SH_CHECK(p.body.anim_status == 3 && p.body.keyframe == 0 && p.upper.anim_status == 3 && p.upper.keyframe == 0);
    SH_CHECK(p.upper_body_state == 0 && p.lower_body_state == 0);
    SH_CHECK(p.body.control_state == 0 && p.upper.state_step == 0);
    SH_CHECK(p.move_speed == 0 && p.afk_timer == 0);
    SH_CHECK(p.body.anim_alpha == 4096 && p.body.anim_time == 0);
}

SH_TEST(exit_anim_aiming_same_weapon_drops_to_twin) {
    ExitAnimPlayer p = sample();
    p.lower_body_state = 20;
    exit_anim_apply(p);
    SH_CHECK(p.upper.anim_status == 60 && p.body.anim_status == 70);
    SH_CHECK(p.upper.keyframe == 9 && p.body.keyframe == 12);
    SH_CHECK(p.upper_body_state == 4 && p.lower_body_state == 20);
    SH_CHECK(p.body.anim_time == 12 * 4096 && p.upper.anim_time == 9 * 4096);

    ExitAnimPlayer q = sample();
    q.lower_body_state = 20;
    q.upper.anim_status = 65;  // below the aiming anims: untouched
    exit_anim_apply(q);
    SH_CHECK(q.upper.anim_status == 65 && q.body.anim_status == 80 && q.body.control_state == 3);
}

SH_TEST(exit_anim_powered_weapon_put_away_loses_power) {
    ExitAnimPlayer p = sample();
    p.weapon_attack = 3;
    exit_anim_apply(p);
    SH_CHECK(p.gas_power_timer == 0 && p.power_effect == 0);
    SH_CHECK(p.is_aiming == 0 && p.state == 0 && p.flags == 0);

    ExitAnimPlayer d = sample();
    d.previous_attack = 12;  // the drill with another input type, re-equipped as plain drill
    d.weapon_attack = 2;
    exit_anim_apply(d);
    SH_CHECK(d.gas_power_timer == 4000 && d.power_effect == 7);
}

SH_TEST(exit_anim_turns_and_other_states_keep_animation) {
    ExitAnimPlayer p = sample();
    p.lower_body_state = 11;
    p.weapon_attack = 1;
    exit_anim_apply(p);
    SH_CHECK(p.state == 1 && p.body.anim_status == 80 && p.move_speed == 300);
    SH_CHECK(p.is_aiming == 0 && p.gas_power_timer == 0);

    ExitAnimPlayer idle = sample();
    idle.state = 2;
    idle.weapon_attack = 1;
    exit_anim_apply(idle);
    SH_CHECK(idle.is_aiming == 1 && idle.gas_power_timer == 4000 && idle.body.anim_status == 80);
    SH_CHECK(idle.body.anim_alpha == 4096 && idle.upper.anim_time == 9 * 4096);
}
