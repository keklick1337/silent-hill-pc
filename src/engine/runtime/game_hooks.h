// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The game's emit points of the mod hook bus (engine/hooks/catalog.h) and the queries mods ask
// the game (sh.game.difficulty, sh.player.facing, sh.world.walkable, sh.entity.spawn: script/host_interfaces.h).
//
// Rule of every emit point: nothing is built and nothing changes unless a handler listens. Events the game has no
// single place for (flags are set in hundreds of places, characters appear through several spawners) are found by
// comparing with the last tick.

#pragma once

namespace sh::hooks {

void install_game_script_hosts();
void game_boot();                 // the boot scripts registered their handlers
void game_new(int difficulty);    // -1 easy, 0 normal, 1 hard
// `quick`: an instant quick save, `slot` its quick slot (1..).
void game_load(int slot, bool quick = false);
void game_save(int slot, bool quick = false);
void game_frame_tick();
void game_map_load(const char* map, bool restored);  // restored: new game, load, continue or demo
void game_room_update(int room);
void game_cutscene_start(const char* game_path);
// item.pickup: false when a handler cancelled it. The count a handler set is what item_pickup_count gives back
// for the same item and flag until the item is added.
bool game_item_pickup(int item, int count, int flag);
int game_item_pickup_count(int item, int flag, int count);
void game_item_picked(int item, int count, int flag);
bool game_item_use(int item);  // false: cancelled
// item.given: a story event, the console or a script adds an item to the inventory without a pickup (source "event",
// "console", "script"). The count to add (a handler may change it), -1 when cancelled.
int game_item_given(int item, int count, const char* source);
// player.quick_heal: the PC's quick heal is about to spend `item` and heal `amount` (Q12; a handler may change it) at
// `health` (Q12). false: cancelled, nothing is spent.
bool game_quick_heal(int item, int health, int& amount);
// game.quick_save / game.quick_load: the quick save or load key is about to open its screen, or with `instant`
// (quick_save_instant) to save or load the quick slot at once. false: cancelled.
bool game_quick_save(bool instant = false);
bool game_quick_load(bool instant = false);
// The damage the player takes (a handler may change or cancel it); attacker_npc: NPC index, -1 none.
int game_player_damage(int amount, int attacker_npc);
void game_player_death();
// map.event: asked every tick a map event runs; emitted once per run.
bool game_map_event_allowed(int param);
// radio.static: the radio is switched on and is about to sound for the monsters it hears (`heard` of them, the
// nearest in slot `nearest_slot`, `nearest_sq` square metres away; -1 and 0 when none). False (a handler cancelled):
// the radio's own noise stays silent this tick.
bool game_radio_static(int heard, int nearest_slot, unsigned nearest_sq);

}  // namespace sh::hooks
