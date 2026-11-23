// SPDX-License-Identifier: GPL-3.0-or-later
//
// Inventory slots with item numbers and counts past a byte: a mod's item numbered
// past 255 keeps its rule, stacks past 255 units merge whole, and a gun past 255 makes its ammo usable.

#include "framework/sh_test.h"

#include "engine/inventory/inventory.h"
#include "engine/inventory/item_actions.h"

using namespace sh::inventory;

namespace {

Slots empty_slots() {
    Slots slots{};
    for (Slot& s : slots) {
        s = Slot{};
    }
    return slots;
}

}  // namespace

SH_TEST(item_rules_hold_numbers_past_a_byte) {
    ItemRules rules;
    rules[300].order = 5;
    rules[300].stack_limit = 500;
    rules[70000].order = 9;
    SH_CHECK(rules[300].order == 5 && rules[300].stack_limit == 500);
    SH_CHECK(rules[70000].order == 9);
    const ItemRules& view = rules;
    SH_CHECK(view[299].order == 0 && view[123456].order == 0);  // unset numbers read the default rule
    SH_CHECK(view[kNoItem].order == 0xFF);                       // empty slots still sort last
    SH_CHECK(view[-1].order == 0);
}

SH_TEST(inventory_merges_counts_past_a_byte) {
    ItemRules rules;
    rules[300].stack_limit = 500;
    Slots slots = empty_slots();
    slots[0] = {300, 200, 0, 0};
    slots[1] = {300, 250, 0, 0};
    Inventory inv(slots, rules);
    SH_CHECK(inv.tidy());
    SH_CHECK(slots[0].item == 300 && slots[0].count == 450);
    SH_CHECK(slots[1].item == kNoItem);
    SH_CHECK(inv.take(300, 1, 40).found && slots[0].count == 449);
    SH_CHECK(!inv.take(44, 1, 40).found);  // 300 is not item 44 (300 & 0xFF)
}

SH_TEST(usable_ammo_of_a_gun_past_a_byte) {
    ItemRules rules;
    rules[300].usable_ammo = UsableAmmo::Loaded;
    rules[300].ammo_item = 301;
    rules[301].usable_ammo = UsableAmmo::WithGun;
    Slots slots = empty_slots();
    slots[0] = {300, 0, 0, 0};
    slots[1] = {301, 400, 0, 0};
    slots[2] = {0, 0, 0, 0};
    const auto slot_at = [&slots](int i) { return slots[static_cast<std::size_t>(i)]; };
    SH_CHECK(has_usable_ammo(slot_at, -1, 0, rules));
    slots[1].count = 0;
    SH_CHECK(!has_usable_ammo(slot_at, -1, 0, rules));
}
