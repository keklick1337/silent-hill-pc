// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The inventory of the running game over the session: the per-item rules built
// from the "inventory" sections of gamedata/items/, the item data behind a run-time number, and the slot operations
// the item screens, the events and the pickups share. The game's entry points (Player_ItemRemove,
// Inventory_ItemsSort, Inventory_AddSpecialItem, ...) are defined over these in inventory_host.cpp.

#pragma once

#include <cstdint>

#include "engine/game_state/save_record.h"
#include "engine/inventory/inventory.h"
#include "engine/items/item_library.h"

namespace sh::inventory::game {

// The rules of every numbered item; empty (and not cached) until the item data is loaded.
const ItemRules& item_rules();

// The item data behind a run-time item number; nullptr for none (or before the data loads).
const items::ItemDef* item_def(int number);

// The save's slots tidied (sorted, merged, packed); the summary of the live save goes to the player's combat record
// and the selected slot. Returns the slot count the screen shows.
std::int32_t tidy(sh::game::SaveRecord& save);
// tidy() of the live save, stored as its slot count.
void tidy_live();

// The live save's slot commands for the item in hand and the map.
void assign_commands();

// One unit of `item` taken from the live save (see Inventory::take); whether the item was there.
bool take(int item, int count);

// The first item with an unlock rule (the base game's Hyper Blaster); -1 when the data has none.
int unlockable_item();
// Inventory::unlock_state / unlock_function of that item: 0 when there is none.
int unlock_state();
int unlock_function();

// Whether the live save holds `item` in any of its slots.
bool holds(int item);

}  // namespace sh::inventory::game
