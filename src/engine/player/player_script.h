// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Harry's Lua (gamedata/chara/Harry) and the engine binds it reaches the player
// through:
//   sh.entity.harry(name) / set_harry(name, v)  the player's values by name (PlayerVar, sh::game::Player);
//   sh.entity.harry_weapon_clip(i)               the equipped weapon's clip keyframes (PlayerState);
//   sh.entity.weapon_number(name), clip_link_set(slot, status, end, link)  (the host);
//   sh.input.control / set_control / controls_clear / player_flag / set_player_flag  the controls of the tick;
//   sh.input.move_stick / pad_mask / pad_direction / pad_button / stick_zone / tick30 / hyper_blaster_mode  the pad.
// The behaviour entry points below run the script's functions where sh::game::Player calls them.

#pragma once

#include "engine/script/sh_script.h"

namespace sh::game {

// What the binds ask the game.
class PlayerScriptHost {
public:
    virtual ~PlayerScriptHost() = default;
    // Run-time number of a weapon (gamedata/weapons/<Name>.json), -1 unknown.
    virtual int weapon_number(const char* name) const = 0;
    // The Hyper Blaster's mode (the inventory's functional test).
    virtual int hyper_blaster_mode() const = 0;
    // The clip entry `status` of the slot's table ends at `end_keyframe` and hands over to `link_status`; false with
    // the reason in `error` when the slot, its table or the status does not exist.
    virtual bool set_clip_link(int slot, long long status, int end_keyframe, int link_status, const char** error) = 0;
};

// Chains the binds in front of the api's named calls.
void bind_player_script(ShScriptGameApi& api, PlayerScriptHost& host);

// Harry's behaviour of the tick in Lua (gamedata/chara/Harry). Without the script Harry does nothing and the
// reason is logged (once per part; the controller's failures are counted and logged throttled).
namespace harry_script {
void controller();      // controller.lua: the pad into the controls of the tick
void legs();            // behavior.lua update: walking, running, turns, side steps, stops
void footsteps();
void upper_body();      // combat.lua: aim, attacks, reload, the upper body clips
void combat_update();   // after Harry is drawn: the weapon's place, effects and the attack against the NPCs
void receive_damage();  // damage.lua: the reactions to the damage of the tick
void before_tick();
bool idle();            // true while Harry stays idle (the tick's state switch ends)
bool state();           // true when the tick ends in the state (game over)
void after_tick();
}  // namespace harry_script

}  // namespace sh::game
