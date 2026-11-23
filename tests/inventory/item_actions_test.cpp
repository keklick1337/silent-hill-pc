// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the item rules the screen and the game ask about (sh::inventory::item_actions).

#include "engine/inventory/item_actions.h"

#include "framework/sh_test.h"

using namespace sh::inventory;

namespace {

constexpr std::uint8_t kGun = 161, kShells = 193, kPistol = 160, kBullets = 192;

ItemRules gun_rules()
{
    ItemRules rules;
    rules[kGun].ammo_item = kShells;
    rules[kGun].clip = 6;
    rules[kGun].keeps_empty_slot = true;
    rules[kGun].usable_ammo = UsableAmmo::Loaded;
    rules[kShells].is_ammo = true;
    rules[kShells].usable_ammo = UsableAmmo::WithGun;
    rules[kPistol].usable_ammo = UsableAmmo::Loaded;
    rules[kBullets].usable_ammo = UsableAmmo::Always;
    return rules;
}

}  // namespace

SH_TEST(inventory_reload_from_slot_both_ways) {
    const ItemRules rules = gun_rules();
    Slots slots{};
    for (Slot& s : slots)
        s.item = kNoItem;
    slots[0] = Slot{kGun, 2, 0, 0};
    slots[1] = Slot{kShells, 3, 0, 0};
    HandAmmo hand{2, 3};
    // The gun: 4 rounds fit, only 3 are there; the ammo slot empties.
    ReloadResult r = reload_from_slot(slots, 8, 0, kGun, hand, rules);
    SH_CHECK(r.ammo_slot == 1 && r.emptied_slot == 1 && slots[0].count == 5 && slots[1].count == 0);
    // The ammo: fills the gun in hand and reports the new rounds.
    slots[1].count = 10;
    r = reload_from_slot(slots, 8, 1, kGun, hand, rules);
    SH_CHECK(r.ammo_slot == -1 && slots[0].count == 6 && slots[1].count == 9 && hand.clip == 6 && hand.reserve == 9);

    HandAmmo equip{0, 0};
    ammo_on_equip(slots, 8, 0, rules, equip);
    SH_CHECK(equip.clip == 6 && equip.reserve == 9);
    SH_CHECK(store_reserve(slots, 8, kGun, 4, rules) == 1 && slots[1].count == 4);
}

SH_TEST(inventory_usable_ammo_scan) {
    const ItemRules rules = gun_rules();
    std::array<Slot, 6> s{Slot{kShells, 5, 0, 0}, Slot{kGun, 0, 0, 0}, Slot{kShells, 5, 0, 0}, Slot{0, 0, 0, 0}};
    auto at = [&](int i) { return s[static_cast<std::size_t>(i)]; };
    // Shells before the gun do not count, after it they do.
    SH_CHECK(has_usable_ammo(at, -1, 0, rules));
    s[2].count = 0;
    SH_CHECK(!has_usable_ammo(at, -1, 0, rules));
    // The gun in hand counts by its clip only.
    SH_CHECK(has_usable_ammo(at, 1, 3, rules) && !has_usable_ammo(at, 1, 0, rules));
    s[2] = Slot{kBullets, 1, 0, 0};
    SH_CHECK(has_usable_ammo(at, -1, 0, rules));
}

SH_TEST(inventory_unlock_rules) {
    UnlockRule rule{true, 1u << 4, true};
    UnlockFacts facts;
    SH_CHECK(unlock_state(rule, false, facts) == 0 && unlock_function(rule, false, facts) == 0);
    facts.past_first_map = true;
    facts.endings = 1u << 4;
    SH_CHECK(unlock_state(rule, false, facts) == 0 && unlock_function(rule, false, facts) == 2);
    // A blaster held on the first map (the console's give) fires by the same unlock; the game never holds one there.
    facts.past_first_map = false;
    SH_CHECK(unlock_function(rule, false, facts) == 0 && unlock_function(rule, true, facts) == 2);
    facts.endings = 0;
    SH_CHECK(unlock_function(rule, true, facts) == 0);
    facts.endings = 1u << 4;
    facts.past_first_map = true;
    facts.cleared = true;
    SH_CHECK(unlock_state(rule, false, facts) == 1 && unlock_state(rule, true, facts) == -1);
    facts = UnlockFacts{true, true, false, 0};
    SH_CHECK(unlock_state(rule, false, facts) == 1 && unlock_function(rule, false, facts) == 1);
}
