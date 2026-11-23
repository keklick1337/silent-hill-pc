// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/inventory/item_actions.h"

#include <algorithm>
#include <vector>

namespace sh::inventory {

namespace {

// Whether `gun` takes `ammo`.
bool uses_ammo(const ItemRules& rules, int gun, int ammo)
{
    return rules[gun].ammo_item == ammo;
}

}  // namespace

void ammo_on_equip(const Slots& slots, int slot_count, int slot, const ItemRules& rules, HandAmmo& hand)
{
    const Slot& gun = slots[static_cast<std::size_t>(slot)];
    if (!rules[gun.item].keeps_empty_slot) {
        hand.clip = 1;
        return;
    }
    hand.clip = gun.count;
    hand.reserve = 0;
    for (int i = 0; i < slot_count; i++) {
        if (slots[static_cast<std::size_t>(i)].item == rules[gun.item].ammo_item)
            hand.reserve = slots[static_cast<std::size_t>(i)].count;
    }
}

int store_reserve(Slots& slots, int slot_count, int gun, std::int32_t reserve, const ItemRules& rules)
{
    for (int i = 0; i < slot_count; i++) {
        Slot& slot = slots[static_cast<std::size_t>(i)];
        if (slot.item == rules[gun].ammo_item) {
            slot.count = reserve;
            return i;
        }
    }
    return -1;
}

bool store_hand_ammo(Slots& slots, int slot_count, int weapon_slot, const HandAmmo& hand, const ItemRules& rules)
{
    if (weapon_slot < 0 || weapon_slot >= static_cast<int>(kSlotCapacity))
        return false;
    Slot& gun = slots[static_cast<std::size_t>(weapon_slot)];
    gun.count = hand.clip;
    // The game scans its slot count; past the forty slots there is only the rest of the save.
    const int scanned = slot_count < static_cast<int>(kSlotCapacity) ? slot_count : static_cast<int>(kSlotCapacity);
    const int ammo = store_reserve(slots, scanned, gun.item, hand.reserve, rules);
    if (ammo < 0)
        return false;
    Slot& stock = slots[static_cast<std::size_t>(ammo)];
    if (stock.count == 0)
        stock.item = kNoItem;
    return true;
}

ExitWeapon weapon_on_exit(int equipped, int weapon_number)
{
    if (equipped == 0)
        return {};
    // The tap input packs as +0, so the attack is the weapon's number itself.
    return {weapon_number, false};
}

ReloadResult reload_from_slot(Slots& slots, int slot_count, int selected, int in_hand, HandAmmo& hand,
                              const ItemRules& rules)
{
    ReloadResult result;
    Slot& chosen = slots[static_cast<std::size_t>(selected)];

    if (rules[chosen.item].is_ammo) {
        for (int i = 0; i < slot_count; i++) {
            Slot& gun = slots[static_cast<std::size_t>(i)];
            if (!uses_ammo(rules, gun.item, chosen.item))
                continue;
            const std::int32_t room = rules[gun.item].clip - gun.count;
            if (chosen.count >= room) {
                gun.count = gun.count + room;
                chosen.count = chosen.count - room;
            } else {
                gun.count = gun.count + chosen.count;
                chosen.count = 0;
            }
            if (gun.item == in_hand) {
                hand.clip = gun.count;
                hand.reserve = chosen.count;
            }
        }
        return result;
    }

    for (int i = 0; i < slot_count; i++) {
        Slot& ammo = slots[static_cast<std::size_t>(i)];
        if (ammo.item != rules[chosen.item].ammo_item)
            continue;
        result.ammo_slot = i;
        const std::int32_t room = rules[chosen.item].clip - chosen.count;
        if (ammo.count >= room) {
            chosen.count = chosen.count + room;
            ammo.count = ammo.count - room;
        } else {
            result.emptied_slot = i;
            chosen.count = chosen.count + ammo.count;
            ammo.count = 0;
        }
    }
    return result;
}

bool has_usable_ammo(const std::function<Slot(int)>& slot_at, int weapon_slot, std::int32_t clip,
                     const ItemRules& rules)
{
    std::vector<int> guns_seen;
    for (int i = 0;; i++) {
        const Slot slot = slot_at(i);
        if (slot.item == 0)
            return false;
        const ItemRule& rule = rules[slot.item];
        if (rule.usable_ammo == UsableAmmo::Loaded &&
            std::find(guns_seen.begin(), guns_seen.end(), slot.item) == guns_seen.end())
            guns_seen.push_back(slot.item);

        if (weapon_slot != -1 && i == weapon_slot) {
            // The item in hand counts by the rounds in its clip, and only that way.
            if (rule.usable_ammo == UsableAmmo::Loaded && clip != 0)
                return true;
            continue;
        }

        switch (rule.usable_ammo) {
            case UsableAmmo::Loaded:
            case UsableAmmo::Always:
                if (slot.count != 0)
                    return true;
                break;
            case UsableAmmo::WithGun:
                for (const int gun : guns_seen) {
                    if (uses_ammo(rules, gun, slot.item) && slot.count != 0)
                        return true;
                }
                break;
            case UsableAmmo::None: break;
        }
    }
}

int unlock_state(const UnlockRule& rule, bool held, const UnlockFacts& facts)
{
    if (held)
        return -1;
    if (facts.past_first_map) {
        if (rule.gun_controller && facts.gun_controller)
            return 1;
        if (facts.cleared && (facts.endings & rule.endings) != 0)
            return 1;
    }
    return 0;
}

int unlock_function(const UnlockRule& rule, bool held, const UnlockFacts& facts)
{
    if (!facts.past_first_map && !held)
        return 0;
    if ((facts.endings & rule.endings) != 0)
        return 2;
    return rule.gun_controller && facts.gun_controller ? 1 : 0;
}

}  // namespace sh::inventory
