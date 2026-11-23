// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the inventory slots (sh::inventory). The rules are
// made up for the tests: item numbers mean nothing to the module.

#include "engine/inventory/inventory.h"

#include <array>
#include <optional>

#include "framework/sh_test.h"

namespace {

using sh::inventory::Command;
using sh::inventory::Inventory;
using sh::inventory::ItemRules;
using sh::inventory::kNoItem;
using sh::inventory::PickupTally;
using sh::inventory::Slots;

constexpr std::uint8_t kDrink = 10;   // stacks to 100
constexpr std::uint8_t kBullets = 20; // stacks to 200
constexpr std::uint8_t kGun = 30;     // keeps its slot when empty, ammo is kBullets
constexpr std::uint8_t kKey = 40;     // does not stack
constexpr std::uint8_t kRadio = 50;

ItemRules test_rules()
{
    ItemRules rules;
    rules[kDrink].order = 1;
    rules[kDrink].stack_limit = 100;
    rules[kDrink].command = Command::UseHealth;
    rules[kBullets].order = 3;
    rules[kBullets].stack_limit = 200;
    rules[kBullets].command = Command::Reload;
    rules[kBullets].command_equipped = Command::UnequipReload;
    rules[kGun].order = 2;
    rules[kGun].keeps_empty_slot = true;
    rules[kGun].reports_ammo = true;
    rules[kGun].ammo_item = kBullets;
    rules[kGun].command = Command::EquipReload;
    rules[kGun].command_equipped = Command::UnequipReload;
    rules[kKey].order = 5;
    rules[kKey].command = Command::UseLook;
    rules[kKey].once_flag = 0x04;
    rules[kRadio].order = 4;
    rules[kRadio].command = Command::OnOff;
    rules[kRadio].map_commands = {{7, Command::Unusable}};
    rules[kRadio].tally = PickupTally::SpecialItem;
    return rules;
}

Slots empty_slots()
{
    Slots slots;
    for (auto& slot : slots)
        slot = {kNoItem, 0, 11, 0xFF};
    return slots;
}

}  // namespace

SH_TEST(inventory_tidy_sorts_merges_and_packs) {
    const ItemRules rules = test_rules();
    Slots slots = empty_slots();
    slots[0] = {kKey, 1, 0, 0};
    slots[1] = {kBullets, 150, 0, 0};
    slots[2] = {kDrink, 60, 0, 0};
    slots[3] = {kBullets, 90, 0, 0};
    slots[4] = {kDrink, 70, 0, 0};
    slots[5] = {kKey, 1, 0, 0};
    slots[6] = {kGun, 0, 0, 0};    // empty gun stays
    slots[7] = {kRadio, 0, 0, 0};  // used up: leaves
    Inventory inventory(slots, rules);
    SH_CHECK(inventory.tidy());
    SH_CHECK(slots[0].item == kDrink && slots[0].count == 100);
    SH_CHECK(slots[1].item == kGun && slots[1].count == 0);
    SH_CHECK(slots[2].item == kBullets && slots[2].count == 200);
    SH_CHECK(slots[3].item == kKey && slots[3].count == 1);
    SH_CHECK(inventory.used_slots() == 4);
    for (int i = 4; i < 40; i++)
        SH_CHECK(slots[i].item == kNoItem && slots[i].count == 0);
}

SH_TEST(inventory_tidy_moves_only_item_and_count) {
    // A merge or a move leaves the command and order bytes of both slots in place.
    const ItemRules rules = test_rules();
    Slots slots = empty_slots();
    slots[0] = {kDrink, 5, 7, 0};
    slots[1] = {kDrink, 6, 9, 0};
    Inventory inventory(slots, rules);
    SH_CHECK(inventory.tidy());
    SH_CHECK(slots[0].item == kDrink && slots[0].count == 11 && slots[0].command == 7);
    SH_CHECK(slots[1].item == kNoItem && slots[1].count == 0 && slots[1].command == 9);
}

SH_TEST(inventory_tidy_keeps_stale_order_of_last_slot) {
    // The last slot's cached order is not refreshed: a stale low order sorts it first.
    const ItemRules rules = test_rules();
    Slots slots = empty_slots();
    slots[0] = {kDrink, 1, 0, 0};
    slots[39] = {kKey, 1, 0, 0};  // order 0 cached, rule says 5
    Inventory inventory(slots, rules);
    SH_CHECK(inventory.tidy());
    SH_CHECK(slots[0].item == kKey && slots[0].order == 0);
    SH_CHECK(slots[1].item == kDrink);
}

SH_TEST(inventory_tidy_of_empty_inventory_reports_unsettled) {
    const ItemRules rules = test_rules();
    Slots slots = empty_slots();
    slots[3] = {kKey, 0, 0, 0};
    Inventory inventory(slots, rules);
    SH_CHECK(!inventory.tidy());
    SH_CHECK(slots[3].item == kNoItem);
}

SH_TEST(inventory_summary_finds_equipped_and_reserve) {
    const ItemRules rules = test_rules();
    Slots slots = empty_slots();
    slots[0] = {kGun, 0, 0, 0};
    slots[1] = {kBullets, 42, 0, 0};
    Inventory inventory(slots, rules);
    auto summary = inventory.summarize(kGun);
    SH_CHECK(summary.shown_slots == 8);
    SH_CHECK(summary.equipped_slot == 0 && summary.ammo_reserve == 42);
    summary = inventory.summarize(kBullets);
    SH_CHECK(summary.equipped_slot == 1 && !summary.ammo_reserve);
    summary = inventory.summarize(std::nullopt);
    SH_CHECK(!summary.equipped_slot);
    slots[1].item = kKey;
    summary = inventory.summarize(kGun);
    SH_CHECK(summary.ammo_reserve == 0);
    for (int i = 0; i < 40; i++)
        slots[i] = {kKey, 1, 0, 0};
    SH_CHECK(inventory.summarize(std::nullopt).shown_slots == 40);
}

SH_TEST(inventory_take_only_counts_one) {
    const ItemRules rules = test_rules();
    Slots slots = empty_slots();
    slots[0] = {kDrink, 2, 0, 0};
    slots[1] = {kKey, 1, 0, 0};
    Inventory inventory(slots, rules);
    auto taken = inventory.take(kDrink, 1, 8);
    SH_CHECK(taken.found && !taken.emptied && slots[0].count == 1);
    taken = inventory.take(kDrink, 0, 8);
    SH_CHECK(taken.found && slots[0].count == 1);
    taken = inventory.take(kDrink, 3, 8);
    SH_CHECK(taken.found && slots[0].count == 1);
    taken = inventory.take(kKey, 1, 1);  // beyond the limit
    SH_CHECK(!taken.found);
    taken = inventory.take(kKey, 1, 8);
    SH_CHECK(taken.found && taken.emptied && slots[1].item == kNoItem);
}

SH_TEST(inventory_last_used_slot_scans_back) {
    const ItemRules rules = test_rules();
    Slots slots = empty_slots();
    slots[2] = {kKey, 1, 0, 0};
    Inventory inventory(slots, rules);
    SH_CHECK(inventory.last_used_slot(6) == 2);
    SH_CHECK(inventory.last_used_slot(1) == 0);
}

SH_TEST(inventory_commands_follow_rules) {
    const ItemRules rules = test_rules();
    Slots slots = empty_slots();
    slots[0] = {kGun, 0, 0, 0};
    slots[1] = {kBullets, 5, 0, 0};
    slots[2] = {kRadio, 1, 0, 0};
    slots[3] = {kKey, 1, 0, 0};
    Inventory inventory(slots, rules);
    inventory.assign_commands(kGun, 3);
    SH_CHECK(slots[0].command == static_cast<std::uint8_t>(Command::UnequipReload));
    SH_CHECK(slots[1].command == static_cast<std::uint8_t>(Command::Reload));
    SH_CHECK(slots[2].command == static_cast<std::uint8_t>(Command::OnOff));
    SH_CHECK(slots[3].command == static_cast<std::uint8_t>(Command::UseLook));
    SH_CHECK(slots[4].command == static_cast<std::uint8_t>(Command::None));
    inventory.assign_commands(-1, 7);
    SH_CHECK(slots[0].command == static_cast<std::uint8_t>(Command::EquipReload));
    SH_CHECK(slots[2].command == static_cast<std::uint8_t>(Command::Unusable));
}

SH_TEST(inventory_pickup_tally_once_flags) {
    const ItemRules rules = test_rules();
    std::uint8_t flags = 0;
    SH_CHECK(sh::inventory::tally_pickup(rules, kKey, flags) == PickupTally::Item && flags == 0x04);
    SH_CHECK(!sh::inventory::tally_pickup(rules, kKey, flags));
    SH_CHECK(sh::inventory::tally_pickup(rules, kRadio, flags) == PickupTally::SpecialItem);
    SH_CHECK(sh::inventory::tally_pickup(rules, kRadio, flags) == PickupTally::SpecialItem && flags == 0x04);
}

// A mod's once-only item has no bit in the pickup flags: an event flag (saved by name) remembers it.
SH_TEST(inventory_pickup_tally_once_event_flag) {
    ItemRules rules = test_rules();
    constexpr std::uint8_t kModItem = 200;
    rules[kModItem].once_event_flag = 1660;  // a bit past the original layout, as a mod's flag gets
    std::uint8_t flags = 0;
    std::array<std::uint32_t, 52> words{};
    SH_CHECK(sh::inventory::tally_pickup(rules, kModItem, flags, words) == PickupTally::Item);
    SH_CHECK(flags == 0 && words[1660 >> 5] == 1u << (1660 & 31));
    SH_CHECK(!sh::inventory::tally_pickup(rules, kModItem, flags, words));
    // Without the save's words the rule cannot be told: it counts.
    SH_CHECK(sh::inventory::tally_pickup(rules, kModItem, flags) == PickupTally::Item);
}

SH_TEST(inventory_reload_moves_reserve) {
    int loaded = 3, reserve = 20;
    sh::inventory::reload(loaded, reserve, 15);
    SH_CHECK(loaded == 15 && reserve == 8);
    loaded = 0, reserve = 4;
    sh::inventory::reload(loaded, reserve, 6);
    SH_CHECK(loaded == 4 && reserve == 0);
    loaded = 6, reserve = 4;
    sh::inventory::reload(loaded, reserve, 6);
    SH_CHECK(loaded == 6 && reserve == 4);
}

SH_TEST(inventory_command_names) {
    SH_CHECK(sh::inventory::command_from_name("EquipReload") == Command::EquipReload);
    SH_CHECK(sh::inventory::command_from_name("Unusable") == Command::Unusable);
    SH_CHECK(!sh::inventory::command_from_name("Drink"));
    SH_CHECK(ItemRules()[kNoItem].order == 0xFF);
}
