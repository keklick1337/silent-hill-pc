// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The inventory of the running game: the live save's slots as
// sh::inventory slots, and the item a slot holds by name. The item screens read and write the
// inventory only through these.
//
// Implemented on the live progress record (game_state_inventory.cpp, save_record.h); the map pack list on the
// overlay header (items_view.cpp).

#pragma once

#include <string>

#include "engine/inventory/inventory.h"

namespace sh::game_state {

// All kSlotCapacity slots (past the slot count the save keeps stale entries; they are returned too).
inventory::Slots inventory_slots();
void set_inventory_slots(const inventory::Slots& slots);

// One slot (0..kSlotCapacity-1).
inventory::Slot inventory_slot(int index);
void set_inventory_slot(int index, const inventory::Slot& slot);

// Slots the inventory screen shows (the save's slot count).
int inventory_shown_slots();

// The id of the item in slot `index` ("silenthill:Handgun"); empty for an empty slot or an item
// without data.
std::string inventory_item_name(int index);

// The current map's item pack list (the items its pack file holds models of, in order, 0-terminated); null without a
// map.
const std::uint8_t* map_loadable_items();

}  // namespace sh::game_state
