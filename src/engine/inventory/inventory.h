// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The player's inventory as slots: forty slots of
// {item, count, command, order}, the save's byte image. This module keeps the slot rules
// of the game — the tidy-up after every change (drop used-up items, sort by display
// order, merge duplicates up to a stack limit, pack to the front), taking one unit,
// the command each slot offers, the pickup tallies and the reload arithmetic. What an
// item does (its order, stack limit, commands, tally) comes from ItemRules, filled by the
// caller from the item data; the module knows no item by name or number.
//
// Standard library only. The game's inventory bridge copies the save's slots in and out
// and applies the reported side effects.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace sh::inventory {

// The item number an empty slot holds. Item numbers are int: a mod's items take numbers past the console's byte
// (sh::ids), 0xFF stays the empty slot and is never given to an item.
inline constexpr int kNoItem = 0xFF;
// Slots of an inventory.
inline constexpr std::size_t kSlotCapacity = 40;
// The inventory screen always offers at least this many slots.
inline constexpr int kMinShownSlots = 8;

// What the command column offers for a slot (the save stores the value).
enum class Command : std::uint8_t {
    UseHealth = 0,      // "Use" of a health item
    Use = 1,
    Equip = 2,
    Unequip = 3,
    EquipReload = 4,    // "Equip" / "Reload"
    UnequipReload = 5,  // "Unequip" / "Reload"
    OnOff = 6,
    Reload = 7,
    Look = 8,
    UseLook = 9,
    Unusable = 10,      // selecting the item is refused
    None = 11,          // empty slot: no command list
};

// One slot: the values of a save's item slot (sh::game::SaveItem).
struct Slot {
    std::int32_t item = kNoItem;
    std::int32_t count = 0;
    std::uint8_t command = 0;  // a Command
    std::uint8_t order = 0;    // display order cached by the last tidy-up

    bool operator==(const Slot&) const = default;
};

using Slots = std::array<Slot, kSlotCapacity>;

// Which pickup counter of the save a pickup adds to (the ending ranking reads both).
enum class PickupTally : std::uint8_t {
    Item,
    SpecialItem,
};

// A command an item offers on one map instead of its usual one.
struct MapCommand {
    int map = 0;
    Command command = Command::Use;
};

// What the inventory screen shows after the item's name.
enum class ItemTag : std::uint8_t { None, Stock, Switch, HyperBlaster };

// How an item counts when the game asks whether the player has any usable gun ammo.
enum class UsableAmmo : std::uint8_t {
    None,
    Loaded,   // a gun: its rounds (in hand: the rounds in the clip)
    Always,   // ammo that counts by itself
    WithGun,  // ammo that counts once a gun using it was seen earlier in the inventory
};

// An item that is unlocked from outside the story (it is handed out when the screen opens).
struct UnlockRule {
    bool enabled = false;
    std::uint32_t endings = 0;   // bits of the save's cleared endings that unlock it
    bool gun_controller = false; // a light gun on the second port unlocks it too
};

// How the inventory treats one item number.
struct ItemRule {
    // Position in the sorted inventory; lower comes first. The empty slot's rule gives the
    // order empty slots sort with.
    std::uint8_t order = 0;
    // Stays in its slot at count 0 (a gun: its rounds are counted in the gun).
    bool keeps_empty_slot = false;
    // Duplicates merge into one slot holding at most this many units; 0: a duplicate slot is
    // dropped and its count lost.
    std::uint16_t stack_limit = 0;
    // When this item is the equipped one, the tidy-up reports its ammo reserve: the count of
    // the first slot holding `ammo_item` (0 when none does or ammo_item < 0).
    bool reports_ammo = false;
    int ammo_item = -1;
    // The command of the slot; on one of `map_commands`' maps that one instead.
    Command command = Command::Use;
    std::vector<MapCommand> map_commands;
    // Set: the item can be in hand, and this is its command while it is.
    std::optional<Command> command_equipped;
    // Pickups: which counter one adds to, and the bit of the save's pickup flags that makes
    // the item count only once (0: counts every time).
    PickupTally tally = PickupTally::Item;
    std::uint8_t once_flag = 0;
    // The same for an item the pickup flags have no bit for (a mod's): the event flag that remembers it, a bit of
    // the save's event flags (-1: none).
    int once_event_flag = -1;
    // The inventory screen: the tag after the name; in hand, the top counter shows this item's
    // count. Item use: used on an event, the item is used up.
    ItemTag tag = ItemTag::None;
    bool stock_in_hand = false;
    bool used_up_on_event = false;
    // Guns and ammo: whether this is ammo, the rounds a gun holds (0xFF: no limit), how it
    // counts as usable ammo. Unlocking (the HyperBlaster).
    bool is_ammo = false;
    std::int32_t clip = 0xFF;
    UsableAmmo usable_ammo = UsableAmmo::None;
    UnlockRule unlock;
};

// The command of a name ("UseHealth", "EquipReload", ...: the enumerator names); nullopt when
// unknown.
std::optional<Command> command_from_name(std::string_view name);

// Rules for every item number; a number no rule was set for reads the default rule. Empty slots sort last
// (order 255).
class ItemRules {
public:
    ItemRules() { (*this)[kNoItem].order = 0xFF; }

    // Grows to hold `item`; a negative number gets a scratch rule nothing reads back.
    ItemRule& operator[](int item);
    const ItemRule& operator[](int item) const;

private:
    std::vector<ItemRule> rules_;
    ItemRule scratch_;
};

// What the inventory screen and the combat code need to know after a tidy-up.
struct Summary {
    // Slots the inventory screen offers: the used ones, at least kMinShownSlots, at most
    // kSlotCapacity.
    int shown_slots = kMinShownSlots;
    // The first slot holding the equipped item, when one is equipped and found.
    std::optional<int> equipped_slot;
    // Ammo reserve of the equipped item, when its rule reports one.
    std::optional<int> ammo_reserve;
};

// Result of taking units of an item.
struct TakeResult {
    bool found = false;    // some slot below the limit holds the item
    bool emptied = false;  // that slot reached 0 and was cleared: tidy up next
};

class Inventory {
public:
    Inventory(Slots& slots, const ItemRules& rules) : slots_(slots), rules_(rules) {}

    // The tidy-up run after every change: used-up items leave, slots sort by order,
    // duplicates merge, used slots pack to the front. false: the inventory is empty, the
    // tidy-up stopped before merging and the game skips its summary.
    bool tidy();

    // Shown slots and where the item in hand is (`equipped`: none when unset).
    Summary summarize(std::optional<int> equipped) const;

    // Takes `count` units of `item` from the first of the first `slot_limit` slots holding
    // it. Only a count of 1 takes anything; 0 (and any other count) just reports whether the
    // item is there, as the game does.
    TakeResult take(int item, int count, int slot_limit);

    // The last used slot at or before `from`, 0 when none is.
    int last_used_slot(int from) const;

    // Sets every slot's command. `equipped`: the item in hand (any value; it is compared with
    // the slots' items), `map`: the current map's number.
    void assign_commands(int equipped, int map);

    // Puts a picked-up item in slot `slot` (no tidy-up).
    void place(int slot, int item, int count);

    // Number of used slots.
    int used_slots() const;

    const Slots& slots() const { return slots_; }

private:
    Slots& slots_;
    const ItemRules& rules_;
};

// The counter a pickup of `item` adds to, given the save's pickup flags and event flag words (updated: the item's
// once-flag or once-event-flag is set); nullopt when the item counted already.
std::optional<PickupTally> tally_pickup(const ItemRules& rules, int item, std::uint8_t& pickup_flags,
                                        std::span<std::uint32_t> event_flags = {});

// Moves rounds from `reserve` into a gun holding `loaded` of `clip`.
void reload(int& loaded, int& reserve, int clip);

}  // namespace sh::inventory
