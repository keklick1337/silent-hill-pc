// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Item rules the inventory screen and the game ask about:
// the rounds a gun takes into the hand, a reload from a slot, whether any usable gun ammo is
// left, and the state of an item unlocked from outside the story. Everything item-specific
// comes from ItemRules (the "inventory" sections of gamedata/items/). Standard library only.

#pragma once

#include <cstdint>
#include <functional>

#include "engine/inventory/inventory.h"

namespace sh::inventory {

// Rounds of the gun in hand: in the clip and in reserve.
struct HandAmmo {
    std::int32_t clip = 0;
    std::int32_t reserve = 0;
};

// What a reload of a slot that is not in hand did.
struct ReloadResult {
    std::int32_t ammo_slot = -1;    // a gun was selected: the (last) slot of its ammo
    std::int32_t emptied_slot = -1; // a gun was selected: the ammo slot it emptied
};

// The item of `slot` goes into the hand: a gun brings its rounds and the count of the last
// slot of its ammo as the reserve (0 when none); anything else counts as one round.
void ammo_on_equip(const Slots& slots, int slot_count, int slot, const ItemRules& rules, HandAmmo& hand);

// After a reload of the gun in hand (`gun`): the first slot of its ammo takes the reserve.
// Returns that slot, -1 when there is none.
int store_reserve(Slots& slots, int slot_count, int gun, std::int32_t reserve, const ItemRules& rules);

// Leaving the screen: the gun in hand (`weapon_slot`, -1: nothing in hand) gets its rounds back
// (`hand.clip`), the first of the first `slot_count` slots (at most kSlotCapacity) holding its
// ammo the reserve, and that slot is freed when the reserve is 0. Nothing is written with nothing
// in hand. Returns whether an ammo slot was written: the caller tidies up then.
bool store_hand_ammo(Slots& slots, int slot_count, int weapon_slot, const HandAmmo& hand, const ItemRules& rules);

// The weapon attack once the screen closes with `equipped` in hand (0: nothing): the tap attack of
// the equipped item's weapon, `weapon_number`; -1 with nothing, and then aiming stops.
struct ExitWeapon {
    std::int32_t attack = -1;
    bool stop_aiming = true;
};
ExitWeapon weapon_on_exit(int equipped, int weapon_number);

// Reload from the screen. Ammo selected: every slot of a gun using it is filled from it (the
// gun in hand, `in_hand`, also updates `hand`). A gun selected: it is loaded from every slot of
// its ammo.
ReloadResult reload_from_slot(Slots& slots, int slot_count, int selected, int in_hand, HandAmmo& hand,
                              const ItemRules& rules);

// Whether any usable gun ammo is left (UsableAmmo rules). `slot_at` reads slots from 0 on; the
// scan ends at the first slot holding item 0, as the game's does (past the 40 slots if none
// does). `weapon_slot` is the slot in hand (-1 none) whose rounds are `clip`.
bool has_usable_ammo(const std::function<Slot(int)>& slot_at, int weapon_slot, std::int32_t clip,
                     const ItemRules& rules);

// Facts the unlock rules test.
struct UnlockFacts {
    bool past_first_map = false;  // not on the game's first map
    bool gun_controller = false;  // a light gun on the second port
    bool cleared = false;         // the game was finished at least once
    std::uint32_t endings = 0;    // bits of the cleared endings
};

// Whether the item may be handed out: -1 it is held already, 1 yes, 0 not yet.
int unlock_state(const UnlockRule& rule, bool held, const UnlockFacts& facts);

// Whether the item works: 2 unlocked by an ending, 1 by the light gun, 0 not.
// The game never hands the item out on its first map and tests nothing there;
// an item `held` there anyway (the console, a mod) works by the same unlocks.
int unlock_function(const UnlockRule& rule, bool held, const UnlockFacts& facts);

}  // namespace sh::inventory
