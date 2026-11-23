// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The inventory of the running game over the session (inventory_host.h): the item rules from gamedata/items/, the
// slot operations on the live progress record and the item questions of the game (using an item on an event or an
// NPC, the long gun's ammo, the unlockable item). The C entry points keep the names the map overlays, events,
// combat and the console call them by.

#include "engine/inventory/game/inventory_host.h"
#include "engine/loader/map_list.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "engine/effects/map_lighting.h"
#include "engine/entities/npc_rules.h"
#include "engine/game_state/game_state_inventory.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/map_event_state.h"
#include "engine/ids/game_numbers.h"
#include "engine/inventory/item_actions.h"
#include "engine/inventory/item_catalog.h"
#include "engine/inventory/item_view.h"
#include "engine/log/sh_log.h"
#include "engine/save/console_records.h"

namespace {

namespace inv = sh::inventory;
using sh::game::SaveRecord;
using sh::game::save_record;
using sh::game::session;

constexpr int kUnequipped = 0;
constexpr int kFirstMap = 0;
constexpr int kTerminalGunController = 3;  // the pad's terminal type of the Konami light gun
constexpr int kInventoryScreenUseStep = 11;  // the item is used: the screen leaves after the use animation
constexpr int kInventoryScreenCantUseStep = 12;
constexpr std::uint32_t kSelectionItem = 0;

// The save keeps one bit per item that counts only once as a pickup (inventory_flags); the bit layout is the save
// format's, not item data.
struct OnceFlag {
    const char* item;
    std::uint8_t bit;
};
constexpr OnceFlag kOnceFlags[] = {
    {"silenthill:PlateOfTurtle", 1u << 0}, {"silenthill:PlateOfHatter", 1u << 1},
    {"silenthill:PlateOfCat", 1u << 2},    {"silenthill:PlateOfQueen", 1u << 3},
    {"silenthill:Lighter", 1u << 4},       {"silenthill:Camera", 1u << 5},
    {"silenthill:VideoTape", 1u << 6},     {"silenthill:ChannelingStone", 1u << 7},
};

// The save's ending bits by name.
std::uint32_t ending_bit(const std::string& name)
{
    static const struct {
        const char* name;
        std::uint32_t bit;
    } kEndings[] = {{"GoodPlus", 1u << 0}, {"Good", 1u << 1}, {"BadPlus", 1u << 2}, {"Bad", 1u << 3}, {"Ufo", 1u << 4}};
    for (const auto& ending : kEndings)
    {
        if (name == ending.name)
            return ending.bit;
    }
    return 0;
}

inv::Command command_named(const sh::items::ItemDef& item, const std::string& name)
{
    if (const std::optional<inv::Command> command = inv::command_from_name(name))
        return *command;
    SH_WARN("[INVENTORY] %s: unknown command \"%s\" (Use assumed)", item.id.c_str(), name.c_str());
    return inv::Command::Use;
}

inv::ItemRules make_rules(const sh::items::ItemLibrary& library)
{
    inv::ItemRules rules;
    for (const sh::items::ItemDef& item : library.items())
    {
        if (item.number <= 0 || item.number == inv::kNoItem)
            continue;
        const sh::items::InventoryDef& def = item.inventory;
        inv::ItemRule& rule = rules[item.number];
        rule.order = def.order;
        rule.keeps_empty_slot = def.keep_when_empty;
        rule.stack_limit = def.stack_limit;
        rule.reports_ammo = def.counts_ammo_in_hand;
        for (const sh::items::WeaponDef& weapon : library.weapons())
        {
            if (library.find_item(weapon.item) != &item)
                continue;
            if (const sh::items::ItemDef* ammo = library.ammo_item_of(weapon); ammo != nullptr && ammo->number != 0)
                rule.ammo_item = ammo->number;
            if (weapon.is_gun())
                rule.clip = weapon.clip ? *weapon.clip : 0xFF;
        }
        rule.is_ammo = item.group == "ammo";
        rule.usable_ammo = def.usable_ammo == "loaded"     ? inv::UsableAmmo::Loaded
                           : def.usable_ammo == "always"   ? inv::UsableAmmo::Always
                           : def.usable_ammo == "with_gun" ? inv::UsableAmmo::WithGun
                                                           : inv::UsableAmmo::None;
        if (def.unlock.enabled)
        {
            rule.unlock.enabled = true;
            rule.unlock.gun_controller = def.unlock.gun_controller;
            for (const std::string& ending : def.unlock.endings)
            {
                const std::uint32_t bit = ending_bit(ending);
                if (bit == 0)
                    SH_WARN("[INVENTORY] %s: unknown ending \"%s\" in unlock", item.id.c_str(), ending.c_str());
                rule.unlock.endings |= bit;
            }
        }
        rule.command = command_named(item, def.command);
        if (!def.command_in_hand.empty())
            rule.command_equipped = command_named(item, def.command_in_hand);
        for (const auto& [map, command] : def.command_on_maps)
        {
            const int map_idx = sh::loader::find_map(map.c_str());
            if (map_idx < 0)
                SH_WARN("[INVENTORY] %s: unknown map \"%s\" in command_on_maps", item.id.c_str(), map.c_str());
            else
                rule.map_commands.push_back({map_idx, command_named(item, command)});
        }
        rule.tag = def.name_tag == "stock"           ? inv::ItemTag::Stock
                   : def.name_tag == "switch"        ? inv::ItemTag::Switch
                   : def.name_tag == "hyper_blaster" ? inv::ItemTag::HyperBlaster
                                                     : inv::ItemTag::None;
        rule.stock_in_hand = def.stock_in_hand;
        rule.used_up_on_event = def.used_up_on_event;
        rule.tally = def.pickup == "special" ? inv::PickupTally::SpecialItem : inv::PickupTally::Item;
        if (def.pickup_once)
        {
            for (const OnceFlag& flag : kOnceFlags)
            {
                if (item.id == flag.item)
                    rule.once_flag = flag.bit;
            }
            // A mod's item counts once through its own flag, which the save keeps by name.
            if (rule.once_flag == 0)
                rule.once_event_flag = sh::ids::flag_number(sh::ids::pickup_once_flag_name(item.id).c_str());
            if (rule.once_flag == 0 && rule.once_event_flag <= 0)
                SH_WARN("[INVENTORY] %s: pickup_once has no save bit (only the base game's own once-only items and "
                        "mods' items have one)",
                        item.id.c_str());
        }
    }
    return rules;
}

inv::Slots load(const SaveRecord& save)
{
    inv::Slots slots;
    for (int i = 0; i < SaveRecord::kItemSlots; i++)
    {
        const sh::game::SaveItem& item = save.items[i];
        slots[static_cast<std::size_t>(i)] = {item.item, item.count, item.command, item.extra};
    }
    return slots;
}

void store(SaveRecord& save, const inv::Slots& slots)
{
    for (int i = 0; i < SaveRecord::kItemSlots; i++)
    {
        const inv::Slot& slot = slots[static_cast<std::size_t>(i)];
        save.items[i] = {slot.item, slot.count, slot.command, slot.order};
    }
}

// Slot `index` past the 40 as the game reads it: four bytes of the save's later fields in the console's form
// (console_records.h); past the record, an item 0 that ends a scan.
inv::Slot overflow_slot(const SaveRecord& save, int index)
{
    std::vector<std::uint8_t> bytes(sh::save::console::kSaveRecordBytes, 0);
    sh::save::console::visit_save_record(sh::save::console::FieldWriter(bytes), 0, save);
    const std::size_t at = static_cast<std::size_t>(index) * 4;
    if (index < 0 || at + 4 > bytes.size())
        return {0, 0, 0, 0};
    return {bytes[at], bytes[at + 1], bytes[at + 2], bytes[at + 3]};
}

// A pickup into slot `index` past the 40: the game writes the item and count bytes over the save's later fields.
// The inventory holds at most 40 used slots, so only slot 40 is reached: its item byte is player_reset (the count
// lands in padding).
void overflow_place(SaveRecord& save, int index, int item, int count)
{
    std::vector<std::uint8_t> bytes(sh::save::console::kSaveRecordBytes, 0);
    const sh::save::console::FieldWriter writer(bytes);
    sh::save::console::visit_save_record(writer, 0, save);
    const std::size_t at = static_cast<std::size_t>(index) * 4;
    if (index < 0 || at + 2 > bytes.size())
        return;
    writer.narrow<std::uint8_t>(at, item);
    writer.narrow<std::uint8_t>(at + 1, count);
    SaveRecord written{};
    sh::save::console::visit_save_record(sh::save::console::FieldReader(bytes), 0, written);
    save.player_reset = written.player_reset;
}

inv::UnlockFacts unlock_facts()
{
    const SaveRecord& save = save_record();
    const sh::game::RawPad& pad = session().game.pads[1].raw;
    inv::UnlockFacts facts;
    facts.past_first_map = save.map > kFirstMap;
    facts.gun_controller = pad.status == 0 && (pad.kind & 0x0F) == 1 && (pad.kind >> 4) == kTerminalGunController;
    facts.cleared = save.clear_count != 0;
    facts.endings = save.endings;
    return facts;
}

// Whether some item's "use_on_npc" rule names this map: there, only those rules apply.
bool npc_use_map(int map)
{
    const sh::items::ItemLibrary* library = inv::item_catalog().library();
    if (library == nullptr)
        return false;
    for (const sh::items::ItemDef& item : library->items())
    {
        if (!item.inventory.use_on_npc.map.empty() && sh::loader::find_map(item.inventory.use_on_npc.map.c_str()) == map)
            return true;
    }
    return false;
}

const sh::items::InventoryDef::UseOnNpc* npc_use(int item, int map)
{
    const sh::items::ItemDef* def = inv::game::item_def(item);
    if (def == nullptr || def->inventory.use_on_npc.map.empty() ||
        sh::loader::find_map(def->inventory.use_on_npc.map.c_str()) != map)
        return nullptr;
    return &def->inventory.use_on_npc;
}

void set_screen_step(std::int32_t step)
{
    session().game.game_steps[1] = step;
    session().game.game_steps[2] = 0;
}

// The item is used on an event: the screen leaves after the use animation.
void use_accepted()
{
    inv::inventory_menu().outline_age = 1;
    inv::inventory_menu().selection = kSelectionItem;
    set_screen_step(kInventoryScreenUseStep);
    sh::effects::map_lighting().set_flashlight(true);
}

}  // namespace

namespace sh::inventory::game {

const ItemRules& item_rules()
{
    static std::optional<ItemRules> s_rules;
    if (!s_rules)
    {
        const items::ItemLibrary* library = item_catalog().library();
        if (library == nullptr)
        {
            // Before gamedata/items/ is loaded (or when it failed): empty rules, not cached.
            static const ItemRules s_none;
            static bool s_warned = false;
            if (!s_warned)
                SH_WARN("[INVENTORY] item data not loaded: the inventory has no item rules");
            s_warned = true;
            return s_none;
        }
        s_rules = make_rules(*library);
    }
    return *s_rules;
}

const items::ItemDef* item_def(int number)
{
    const items::ItemLibrary* library = item_catalog().library();
    if (library == nullptr || number == 0)
        return nullptr;
    for (const items::ItemDef& item : library->items())
    {
        if (item.number == number)
            return &item;
    }
    return nullptr;
}

std::int32_t tidy(SaveRecord& save)
{
    Slots slots = load(save);
    const bool settled = Inventory(slots, item_rules()).tidy();
    store(save, slots);
    if (!settled)
        return kMinShownSlots;

    // The summary always reads the live save, whichever one was tidied.
    SaveRecord& live = save_record();
    Slots current = load(live);
    const std::optional<int> equipped =
        live.equipped_weapon != kUnequipped ? std::optional<int>(live.equipped_weapon) : std::nullopt;
    const Summary summary = Inventory(current, item_rules()).summarize(equipped);
    sh::game::PlayerCombat& combat = session().system.combat;
    if (summary.equipped_slot)
        combat.weapon_slot = static_cast<std::int8_t>(*summary.equipped_slot);
    if (summary.ammo_reserve)
        combat.ammo_total = *summary.ammo_reserve;
    if (summary.shown_slots - 1 < static_cast<std::int32_t>(session().system.inventory_selected))
        session().system.inventory_selected = static_cast<std::uint8_t>(summary.shown_slots - 1);
    return summary.shown_slots;
}

void tidy_live()
{
    save_record().slot_count = static_cast<std::uint8_t>(tidy(save_record()));
}

void assign_commands()
{
    Slots slots = load(save_record());
    Inventory(slots, item_rules()).assign_commands(inventory_menu().equipped_item, save_record().map);
    store(save_record(), slots);
}

bool take(int item, int count)
{
    Slots slots = load(save_record());
    const TakeResult taken = Inventory(slots, item_rules()).take(item, count, save_record().slot_count);
    store(save_record(), slots);
    if (taken.emptied)
        tidy_live();
    return taken.found;
}

int unlockable_item()
{
    const items::ItemLibrary* library = item_catalog().library();
    if (library == nullptr)
        return -1;
    const ItemRules& rules = item_rules();
    int first = -1;
    for (const items::ItemDef& item : library->items())
    {
        if (item.number >= 0 && rules[item.number].unlock.enabled && (first < 0 || item.number < first))
            first = item.number;
    }
    return first;
}

bool holds(int item)
{
    for (const sh::game::SaveItem& slot : save_record().items)
    {
        if (slot.item == item)
            return true;
    }
    return false;
}

int unlock_state()
{
    const int item = unlockable_item();
    if (item < 0)
        return 0;
    return sh::inventory::unlock_state(item_rules()[item].unlock, holds(item), unlock_facts());
}

int unlock_function()
{
    const int item = unlockable_item();
    if (item < 0)
        return 0;
    return sh::inventory::unlock_function(item_rules()[item].unlock, holds(item), unlock_facts());
}

}  // namespace sh::inventory::game

namespace igame = sh::inventory::game;

extern "C" {

void Inventory_AddSpecialItem(int item, int count);

bool Player_ItemRemove(int item, int count)
{
    return igame::take(item, count);
}

std::int32_t Inventory_ItemsSort(void* save)
{
    return igame::tidy(*static_cast<SaveRecord*>(save));
}

void Inventory_AddSpecialItem(int item, int count)
{
    SaveRecord& save = save_record();
    std::uint8_t flags = save.inventory_flags;
    const std::optional<inv::PickupTally> tally = inv::tally_pickup(igame::item_rules(), item, flags, save.event_flags);
    save.inventory_flags = flags;
    if (tally == inv::PickupTally::SpecialItem)
    {
        // Bits 3..7 of the mode byte, a 5-bit counter that wraps.
        const std::uint8_t special = static_cast<std::uint8_t>(((save.mode_bits >> 3) + 1) & 0x1F);
        save.mode_bits = static_cast<std::uint8_t>((save.mode_bits & 0x07) | (special << 3));
    }
    else if (tally == inv::PickupTally::Item)
        save.picked_up_items++;

    const int slot = save.slot_count;
    if (slot < SaveRecord::kItemSlots)
    {
        inv::Slots slots = load(save);
        inv::Inventory(slots, igame::item_rules()).place(slot, item, count);
        store(save, slots);
    }
    else
    {
        // A full inventory: the game writes past the slots, into the save fields after them.
        overflow_place(save, slot, item, count);
    }
    igame::tidy_live();
}

void Items_AmmoReloadCalculation(std::int32_t* loaded, std::int32_t* reserve, std::int32_t gun)
{
    int clip = *loaded;
    int rest = *reserve;
    inv::reload(clip, rest, inv::item_catalog().gun_clip(gun));
    *loaded = clip;
    *reserve = rest;
}

void Inventory_ItemUse(std::int32_t slot)
{
    sh::entities::CharacterSlots& characters = session().system.characters;
    const sh::entities::CharacterRecord& player = characters.player;
    SaveRecord& save = save_record();
    const int item = save.items[slot].item;

    // "Can't use it here" unless something takes the item.
    set_screen_step(kInventoryScreenCantUseStep);

    if (npc_use_map(save.map))
    {
        // As in the US release, any NPC in the first slot accepts it (later releases check who).
        const sh::items::InventoryDef::UseOnNpc* rule = npc_use(item, save.map);
        const sh::entities::CharacterRecord& npc = characters.npcs[0];
        if (rule != nullptr &&
            !sh::entities::outside_radius(player.position.x, player.position.z, npc.position.x, npc.position.z,
                                          static_cast<std::int32_t>(rule->distance * 4096.0)) &&
            std::abs(player.position.y - npc.position.y) < static_cast<std::int32_t>(rule->height * 4096.0) &&
            characters.player_upper.state == 0)
        {
            use_accepted();
            const int flag = sh::ids::flag_number(rule->flag.c_str());
            if (flag >= 0)
                save.set_event_flag(flag, true);
        }
        return;
    }

    // An active event asking for this item takes it (the player's last used item for the event update).
    const auto& asked = sh::gameflow::map_event_state().item_ids;
    for (std::size_t i = 0; asked[i] != -1; i++)
    {
        if (asked[i] == item)
        {
            use_accepted();
            characters.player_upper.last_used_item = asked[i];
            break;
        }
    }
}

bool Inventory_HasUsableLongGunAmmo(bool /*unused*/)
{
    // The scan reads the save's slots as the game does, beyond the 40 when no slot holds item 0.
    SaveRecord& save = save_record();
    const auto slot_at = [&save](int i) -> inv::Slot {
        if (i < SaveRecord::kItemSlots)
        {
            const sh::game::SaveItem& item = save.items[i];
            return {item.item, item.count, item.command, item.extra};
        }
        return overflow_slot(save, i);
    };
    const sh::game::PlayerCombat& combat = session().system.combat;
    return inv::has_usable_ammo(slot_at, combat.weapon_slot, combat.ammo_loaded, igame::item_rules());
}

std::int32_t Inventory_HyperBlasterUnlockTest(void)
{
    return igame::unlock_state();
}

std::int32_t Inventory_HyperBlasterFunctionalTest(void)
{
    return igame::unlock_function();
}

void Inventory_UnlockedItemsGrant(void)
{
    const sh::items::ItemLibrary* library = inv::item_catalog().library();
    if (library == nullptr)
        return;
    std::vector<int> numbers;
    for (const sh::items::ItemDef& item : library->items())
    {
        if (item.number >= 0)
            numbers.push_back(item.number);
    }
    // In number order, as the game's scan of every item number grants them.
    std::sort(numbers.begin(), numbers.end());
    const inv::ItemRules& rules = igame::item_rules();
    for (const int number : numbers)
    {
        if (rules[number].unlock.enabled &&
            inv::unlock_state(rules[number].unlock, igame::holds(number), unlock_facts()) == 1)
            Inventory_AddSpecialItem(number, 1);
    }
}

}  // extern "C"
