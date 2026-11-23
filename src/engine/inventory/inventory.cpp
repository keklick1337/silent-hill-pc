// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/inventory/inventory.h"

#include <algorithm>
#include <utility>

namespace sh::inventory {

namespace {

constexpr int kCapacity = static_cast<int>(kSlotCapacity);

bool used(const Slot& slot) { return slot.item != kNoItem; }

void clear(Slot& slot)
{
    slot.item = kNoItem;
    slot.count = 0;
}

}  // namespace

ItemRule& ItemRules::operator[](int item)
{
    if (item < 0) {
        scratch_ = ItemRule{};
        return scratch_;
    }
    if (static_cast<std::size_t>(item) >= rules_.size())
        rules_.resize(static_cast<std::size_t>(item) + 1);
    return rules_[static_cast<std::size_t>(item)];
}

const ItemRule& ItemRules::operator[](int item) const
{
    static const ItemRule kDefault;
    const auto at = static_cast<std::size_t>(item);
    return item >= 0 && at < rules_.size() ? rules_[at] : kDefault;
}

bool Inventory::tidy()
{
    for (Slot& slot : slots_) {
        if (slot.count == 0 && !rules_[slot.item].keeps_empty_slot)
            slot.item = kNoItem;
    }

    // The game refreshes the cached order of all slots but the last one, and sorts on the
    // cached bytes, so a stale order in the last slot takes part in the sort.
    for (int i = 0; i < kCapacity - 1; i++)
        slots_[i].order = rules_[slots_[i].item].order;

    // Exchange sort on the cached order: whole slots move.
    for (int i = 0; i < kCapacity - 1; i++) {
        for (int j = i + 1; j < kCapacity; j++) {
            if (slots_[i].order > slots_[j].order)
                std::swap(slots_[i], slots_[j]);
        }
    }

    // The first used slot moves to the front; only item and count move, the command and
    // order bytes stay where they are.
    auto first = std::find_if(slots_.begin(), slots_.end(), used);
    if (first == slots_.end())
        return false;
    if (first != slots_.begin()) {
        slots_[0].item = first->item;
        slots_[0].count = first->count;
        clear(*first);
    }

    // Each slot either merges into an earlier slot of the same item or moves into the
    // first earlier empty one, whichever comes first; item and count only.
    for (int i = 1; i < kCapacity; i++) {
        for (int j = 0; j < i && used(slots_[i]); j++) {
            Slot& earlier = slots_[j];
            Slot& later = slots_[i];
            if (earlier.item == later.item) {
                const int limit = rules_[earlier.item].stack_limit;
                if (limit != 0)
                    earlier.count = std::min(earlier.count + later.count, limit);
                clear(later);
            } else if (!used(earlier)) {
                earlier.item = later.item;
                earlier.count = later.count;
                clear(later);
            }
        }
    }

    return true;
}

Summary Inventory::summarize(std::optional<int> equipped) const
{
    Summary summary;
    if (equipped) {
        for (int i = 0; i < kCapacity; i++) {
            if (slots_[i].item != *equipped)
                continue;
            summary.equipped_slot = i;
            const ItemRule& rule = rules_[*equipped];
            if (rule.reports_ammo) {
                summary.ammo_reserve = 0;
                for (const Slot& slot : slots_) {
                    if (slot.item == rule.ammo_item) {
                        summary.ammo_reserve = slot.count;
                        break;
                    }
                }
            }
            break;
        }
    }
    summary.shown_slots = std::clamp(used_slots(), kMinShownSlots, kCapacity);
    return summary;
}

std::optional<Command> command_from_name(std::string_view name)
{
    static constexpr std::pair<std::string_view, Command> kNames[] = {
        {"UseHealth", Command::UseHealth}, {"Use", Command::Use}, {"Equip", Command::Equip},
        {"Unequip", Command::Unequip}, {"EquipReload", Command::EquipReload},
        {"UnequipReload", Command::UnequipReload}, {"OnOff", Command::OnOff}, {"Reload", Command::Reload},
        {"Look", Command::Look}, {"UseLook", Command::UseLook}, {"Unusable", Command::Unusable},
        {"None", Command::None},
    };
    for (const auto& [known, command] : kNames) {
        if (known == name)
            return command;
    }
    return std::nullopt;
}

TakeResult Inventory::take(int item, int count, int slot_limit)
{
    TakeResult result;
    const int limit = std::min(slot_limit, kCapacity);
    for (int i = 0; i < limit; i++) {
        Slot& slot = slots_[i];
        if (slot.item != item)
            continue;
        result.found = true;
        // A slot already at 0 (a gun kept empty) has nothing to give; the console's byte wrapped it to 255.
        if (count == 1 && slot.count > 0) {
            slot.count--;
            if (slot.count == 0) {
                slot.item = kNoItem;
                result.emptied = true;
            }
        }
        break;
    }
    return result;
}

int Inventory::last_used_slot(int from) const
{
    for (int i = std::min(from, kCapacity - 1); i >= 0; i--) {
        if (used(slots_[i]))
            return i;
    }
    return 0;
}

void Inventory::assign_commands(int equipped, int map)
{
    for (Slot& slot : slots_) {
        if (!used(slot)) {
            slot.command = static_cast<std::uint8_t>(Command::None);
            continue;
        }
        const ItemRule& rule = rules_[slot.item];
        Command command = rule.command;
        if (rule.command_equipped && slot.item == equipped) {
            command = *rule.command_equipped;
        } else {
            for (const MapCommand& on_map : rule.map_commands) {
                if (on_map.map == map) {
                    command = on_map.command;
                    break;
                }
            }
        }
        slot.command = static_cast<std::uint8_t>(command);
    }
}

void Inventory::place(int slot, int item, int count)
{
    slots_[slot].item = item;
    slots_[slot].count = count;
}

int Inventory::used_slots() const
{
    return static_cast<int>(std::count_if(slots_.begin(), slots_.end(), used));
}

std::optional<PickupTally> tally_pickup(const ItemRules& rules, int item, std::uint8_t& pickup_flags,
                                        std::span<std::uint32_t> event_flags)
{
    const ItemRule& rule = rules[item];
    if (rule.once_flag != 0) {
        if ((pickup_flags & rule.once_flag) != 0)
            return std::nullopt;
        pickup_flags = static_cast<std::uint8_t>(pickup_flags | rule.once_flag);
    }
    const auto word = static_cast<std::size_t>(rule.once_event_flag >> 5);
    if (rule.once_event_flag > 0 && word < event_flags.size()) {
        const std::uint32_t bit = 1u << (rule.once_event_flag & 31);
        if ((event_flags[word] & bit) != 0)
            return std::nullopt;
        event_flags[word] |= bit;
    }
    return rule.tally;
}

void reload(int& loaded, int& reserve, int clip)
{
    const int missing = clip - loaded;
    if (missing == 0)
        return;
    const int moved = reserve >= missing ? missing : reserve;
    loaded += moved;
    reserve -= moved;
}

}  // namespace sh::inventory
