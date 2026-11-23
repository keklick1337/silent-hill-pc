// SPDX-License-Identifier: GPL-3.0-or-later
//
// The PC's quick actions outside the inventory: manual reload, weapon
// cycling, the optional number keys and wheel for weapons, quick heal, the flashlight toggle and the quick turn
// request. The input layer reports this frame's presses; the engine decides and acts on the running game through
// QuickActionsHost. Items are numbers of the running game, looked up by name (gamedata/items); the engine names
// none of them itself: the orders and the heal policy are gamedata/config/quick_actions.json (QuickActionsData).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "engine/input/expiring_latch.h"
#include "engine/items/quick_heal.h"

namespace sh::items {

class QuickActionsHost {
public:
    virtual ~QuickActionsHost() = default;

    // In gameplay with the player in control (the reload latch is dropped otherwise).
    virtual bool player_in_control() = 0;
    // Safe to switch weapons or heal: in control and the upper body idle or moving (not aiming, swinging or
    // reloading: the weapon swap loads its model).
    virtual bool action_safe() = 0;
    // A manual reload would do something now: a gun in hand, reserve ammo, a clip that is not full, no reload
    // running.
    virtual bool reload_possible() = 0;
    virtual std::int32_t frame_time() = 0;  // Q12 seconds
    virtual bool console_open() = 0;
    virtual bool debug_menu_open() = 0;
    virtual bool keyframe_viewer() = 0;  // the debug keyframe viewer owns the '-' key
    virtual bool quick_weapons_enabled() = 0;

    // The inventory: slots shown (the live ones), the item number and amount of a slot.
    virtual int slot_count() = 0;
    virtual int slot_item(int slot) = 0;
    virtual int slot_amount(int slot) = 0;
    virtual int equipped_item() = 0;
    virtual int item_number(std::string_view name) = 0;  // -1 for an unknown item
    virtual bool is_gun(int item) = 0;
    virtual int ammo_item(int item) = 0;  // the gun's ammunition item, -1 for none
    virtual bool fills_saturation(int item) = 0;

    // Puts the weapon in slot `slot` in Harry's hand: the weapon state, the held model and the motor reset of the
    // outgoing weapon, as the inventory does.
    virtual void equip(int item, int slot, int ammo, int reserve) = 0;

    virtual std::int32_t health() = 0;  // Q12
    virtual void set_health(std::int32_t health) = 0;
    virtual void set_saturation(std::int32_t saturation) = 0;
    virtual std::int32_t heal_amount(int item) = 0;  // Q12, gamedata/config/gameplay.json
    virtual std::int32_t max_health() = 0;
    virtual std::int32_t max_saturation() = 0;
    // Takes one of `item` out of the inventory through the game's own removal; false when none was there.
    // The mods' say before a heal (hook player.quick_heal): false keeps the item; `amount` (Q12) may change.
    virtual bool quick_heal_allowed(int item, std::int32_t health, std::int32_t& amount) {
        (void)item, (void)health, (void)amount;
        return true;
    }
    virtual bool spend_item(int item) = 0;
    virtual void heal_feedback() = 0;  // the heal sound and the green screen pulse

    virtual void toggle_flashlight() = 0;
    virtual void set_quick_turn_request(bool request) = 0;

    virtual void log(const std::string& line) = 0;
    virtual void toast(const std::string& line) = 0;
    // gamedata/config/quick_actions.json.
    virtual const QuickActionsData& quick_data() = 0;
};

// This frame's presses (rising edges) of the quick action binds.
struct QuickActionPresses {
    bool cycle_weapons = false;
    bool quick_heal = false;
    bool quick_turn = false;
    bool flashlight = false;
    bool reload = false;
    int weapon_key = -1;  // index into QuickActionsData::weapon_keys of the last number key pressed, -1 none
    int wheel = 0;        // wheel notches since the last frame (positive: up)
};

// The weapon keys: number keys 1..0 and '-' (their weapons: QuickActionsData::weapon_keys).
constexpr int kQuickWeaponCount = 11;

class QuickActions {
public:
    static constexpr std::int32_t kReloadWindow = 1228;  // Q12 seconds a reload press stays latched (0.30 s)

    // One frame, in the engine's order: reload latch, quick weapons, then (in safe gameplay) cycle, heal,
    // flashlight and the quick turn request.
    void frame(const QuickActionPresses& presses, QuickActionsHost& host);

    // Equip an owned weapon now (console EQUIP); false when it is not owned or not safe.
    bool equip_owned(int item, QuickActionsHost& host);
    void cycle_weapons(QuickActionsHost& host);
    void quick_heal(QuickActionsHost& host);

    // The latched manual reload request (the combat machine consumes it); a raw set leaves the countdown.
    bool reload_requested() const { return reload_.active(); }
    void set_reload_requested(bool on) { reload_.set_active(on); }

private:
    void quick_weapons(const QuickActionPresses& presses, QuickActionsHost& host);
    void equip_slot(int item, int slot, QuickActionsHost& host);

    input::ExpiringLatch reload_;
};

// The first live slot holding `item`, -1 when none.
int find_item_slot(QuickActionsHost& host, int item);

// The game's quick actions (src/input/pc_quick_actions.cpp) and the running game they act on (over the game's
// records).
QuickActions& quick_actions();
QuickActionsHost& game_quick_actions_host();

}  // namespace sh::items
