// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The inventory screen's item carousel over the session: the timing of
// scrolls and item actions and the model poses come from sh::inventory::update_carousel; this host does the game
// side of each action (equip, reload, heal) with the ammo arithmetic of the item rules (item_actions.h) and healing
// from gamedata/config/gameplay.json. Inventory_PlayerItemScroll keeps the name the screen draw and the mouse call.

#include <algorithm>
#include <cstdint>

#include "audio/game/game_sound_driver.h"
#include "engine/config/gameplay_config.h"
#include "engine/game_state/game_state_inventory.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/game/game_link.h"
#include "engine/inventory/game/inventory_host.h"
#include "engine/inventory/game/item_stage.h"
#include "engine/inventory/item_actions.h"
#include "engine/inventory/item_carousel.h"
#include "engine/inventory/item_catalog.h"
#include "engine/inventory/item_view.h"

extern "C" {
void Items_AmmoReloadCalculation(std::int32_t* loaded, std::int32_t* reserve, std::int32_t gun);
void Player_WeaponModelStateSet(std::int32_t held_attack, std::int8_t event);
}

namespace {

namespace inv = sh::inventory;
using sh::game::save_record;
using sh::game::session;

constexpr std::uint16_t kHealSound = 1325;
constexpr int kEmptyItem = 0xFF;
constexpr int kUnequipped = 0;
constexpr std::uint32_t kSelectionItem = 0;
// Format constant, not data: the packed attack byte is the weapon item's number + 128 wrapped to 8 bits (weapon
// items are 128..255, so the attack is item - 128); every reader of weapon_attack decodes it the same way.
constexpr std::int32_t kWeaponOffset = 0x80;

inv::Slots load_slots() { return sh::game_state::inventory_slots(); }

// Only the item and count go back: the actions never change a slot's command or order.
void store_slots(const inv::Slots& slots)
{
    for (int i = 0; i < static_cast<int>(slots.size()); i++)
    {
        inv::Slot slot = sh::game_state::inventory_slot(i);
        slot.item = slots[static_cast<std::size_t>(i)].item;
        slot.count = slots[static_cast<std::size_t>(i)].count;
        sh::game_state::set_inventory_slot(i, slot);
    }
}

sh::game::PlayerCombat& combat() { return session().system.combat; }

// The packed tap attack of an item in hand: the number of the weapon the data carries as it (ItemCatalog::held_number);
// without item data, the original's numbering.
std::int32_t held_attack(int item)
{
    if (inv::item_catalog().loaded())
        return inv::item_catalog().held_number(item);
    return static_cast<std::int8_t>(static_cast<std::uint8_t>(item + kWeaponOffset));
}

// The item held as a packed tap attack (the inverse of held_attack).
int held_item(std::int32_t attack)
{
    if (inv::item_catalog().loaded())
        return inv::item_catalog().held_item(attack);
    return static_cast<std::uint8_t>(attack + kWeaponOffset);
}

inv::HandAmmo hand_ammo() { return {combat().ammo_loaded, combat().ammo_total}; }

void set_hand_ammo(const inv::HandAmmo& hand)
{
    combat().ammo_loaded = hand.clip;
    combat().ammo_total = hand.reserve;
}

class CarouselHost final : public inv::CarouselHost {
public:
    explicit CarouselHost(std::uint32_t* selection) : selection_(selection) {}

    std::int32_t step() override { return session().game.game_steps[1]; }
    std::int32_t selected_slot() override { return session().system.inventory_selected; }
    std::int32_t slot_count() override { return save_record().slot_count; }
    std::int32_t display_slot(int display) override { return inv::item_view().shown[static_cast<std::size_t>(display)]; }
    std::int32_t weapon_slot() override { return combat().weapon_slot; }

    inv::Slot slot(std::int32_t index) override { return sh::game_state::inventory_slot(index); }

    void scale_model(int display, std::int32_t scale) override
    {
        inv::item_stage().place(display).transform.scale = {scale, scale, scale};
    }

    void place_model(int display, std::int32_t x, std::int32_t y, std::int32_t z) override
    {
        std::int32_t* t = inv::item_stage().place(display).frame.local.t;
        t[0] = x;
        t[1] = y;
        t[2] = z;
    }

    void turn_model(int display, std::int32_t angle) override
    {
        inv::item_stage().place(display).transform.rotate[1] = static_cast<std::int16_t>(angle);
    }
    void spin_model(int display, std::int32_t delta) override
    {
        std::int16_t& y = inv::item_stage().place(display).transform.rotate[1];
        y = static_cast<std::int16_t>(y + delta);
    }

    void set_step(std::int32_t step, bool reset_substep) override
    {
        session().game.game_steps[1] = step;
        if (reset_substep)
            session().game.game_steps[2] = 0;
    }

    void select_item_row() override { *selection_ = kSelectionItem; }
    void reset_selection_age() override { inv::inventory_menu().outline_age = 0; }
    bool files_loaded() override { return sh::gameflow::game::file_queue_length() == 0; }

    bool confirm_clicked() override
    {
        const sh::game::PadBinds& binds = session().game.settings.binds;
        return (session().game.pads[0].clicked & (binds.action | binds.cancel)) != 0;
    }

    void set_slot_count(std::int32_t index, std::int32_t count) override { save_record().items[index].count = count; }
    void empty_slot(std::int32_t index) override { save_record().items[index].item = kEmptyItem; }
    void refresh_commands() override { inv::game::assign_commands(); }

    bool is_ammo(std::int32_t index) override { return inv::game::item_rules()[save_record().items[index].item].is_ammo; }

    bool used_up_on_use(std::int32_t index) override
    {
        return inv::game::item_rules()[save_record().items[index].item].used_up_on_event;
    }

    void equip_start() override
    {
        const std::int32_t selected = session().system.inventory_selected;
        combat().weapon_attack = held_attack(save_record().items[selected].item);
        combat().weapon_slot = static_cast<std::int8_t>(selected & 0xFF);
        inv::item_view().equipped() = selected;

        inv::item_stage().link_from_pack(inv::kEquippedModel, save_record().items[selected].item);

        // The equipped slot is the slot display 7 shows, which is now the selected one.
        inv::HandAmmo hand = hand_ammo();
        inv::ammo_on_equip(load_slots(), save_record().slot_count, inv::item_view().equipped(), inv::game::item_rules(),
                           hand);
        set_hand_ammo(hand);
    }

    void equip_finish() override
    {
        const int item = save_record().items[session().system.inventory_selected].item;
        inv::inventory_menu().equipped_item = item;
        save_record().equipped_weapon = item;
        combat().weapon_attack = held_attack(item);
        combat().weapon_slot = static_cast<std::int8_t>(session().system.inventory_selected);
        inv::game::assign_commands();
        Player_WeaponModelStateSet(0, -1);
    }

    void unequip_finish() override
    {
        inv::inventory_menu().equipped_item = kUnequipped;
        save_record().equipped_weapon = kUnequipped;
        combat().weapon_attack = -1;
        combat().weapon_slot = -1;
        inv::game::assign_commands();
        Player_WeaponModelStateSet(0, -1);
    }

    std::int32_t reload_equipped() override
    {
        std::int32_t clip = combat().ammo_loaded;
        std::int32_t reserve = combat().ammo_total;
        Items_AmmoReloadCalculation(&clip, &reserve, combat().weapon_attack);

        combat().ammo_total = reserve;
        combat().ammo_loaded = clip;
        save_record().items[combat().weapon_slot].count = clip;

        inv::Slots slots = load_slots();
        const int ammo = inv::store_reserve(slots, save_record().slot_count, save_record().items[combat().weapon_slot].item,
                                            combat().ammo_total, inv::game::item_rules());
        store_slots(slots);
        return ammo;
    }

    inv::ReloadResult reload_selected() override
    {
        inv::Slots slots = load_slots();
        inv::HandAmmo hand = hand_ammo();
        const inv::ReloadResult result = inv::reload_from_slot(
            slots, save_record().slot_count, session().system.inventory_selected,
            held_item(combat().weapon_attack), hand, inv::game::item_rules());
        store_slots(slots);
        set_hand_ammo(hand);
        return result;
    }

    void use_health() override
    {
        sh::game::SaveItem& item = save_record().items[session().system.inventory_selected];
        if (inv::game::item_rules()[item.item].command != inv::Command::UseHealth)
            return;
        std::int32_t& health = session().system.characters.player.health;
        health += Gameplay_HealAmountQ12(item.item);
        const sh::items::ItemDef* def = inv::game::item_def(item.item);
        if (def != nullptr && def->inventory.fills_saturation)
            save_record().health_saturation = Gameplay_MaxSaturationQ12();
        sh::audio::game_sound_driver().play_effect(kHealSound, -0x40, 0x40);
        health = std::clamp(health, 0, Gameplay_MaxHealthQ12());
        item.count--;
    }

private:
    std::uint32_t* selection_;
};

}  // namespace

extern "C" void Inventory_PlayerItemScroll(std::uint32_t* selection)
{
    CarouselHost host(selection);
    inv::update_carousel(inv::item_screen().carousel, host);
}
