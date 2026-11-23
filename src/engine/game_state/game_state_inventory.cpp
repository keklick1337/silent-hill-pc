// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// sh::game_state's inventory (game_state_inventory.h) over the live progress record (save_record.h).
// map_loadable_items() is the map runtime's.

#include "engine/game_state/game_state_inventory.h"

#include "engine/game_state/save_record.h"
#include "engine/inventory/item_catalog.h"
#include "engine/maps/map_runtime.h"

namespace sh::game_state {

namespace {
constexpr int kEmptyItem = 0xFF;
}

inventory::Slot inventory_slot(int index) {
    const game::SaveItem& item = game::save_record().items[index];
    return {item.item, item.count, item.command, item.extra};
}

void set_inventory_slot(int index, const inventory::Slot& slot) {
    game::SaveItem& item = game::save_record().items[index];
    item.item = slot.item;
    item.count = slot.count;
    item.command = slot.command;
    item.extra = slot.order;
}

inventory::Slots inventory_slots() {
    inventory::Slots slots;
    for (int i = 0; i < game::SaveRecord::kItemSlots; i++) slots[static_cast<std::size_t>(i)] = inventory_slot(i);
    return slots;
}

void set_inventory_slots(const inventory::Slots& slots) {
    for (int i = 0; i < game::SaveRecord::kItemSlots; i++) set_inventory_slot(i, slots[static_cast<std::size_t>(i)]);
}

int inventory_shown_slots() { return game::save_record().slot_count; }

std::string inventory_item_name(int index) {
    const int id = game::save_record().items[index].item;
    if (id == kEmptyItem || id == 0) return {};
    const items::ItemDef* item = inventory::item_catalog().item(id);
    return item != nullptr ? item->id : std::string();
}

const std::uint8_t* map_loadable_items() { return maps::map_runtime().loadable_items(); }

}  // namespace sh::game_state
