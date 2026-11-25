// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The item carousel of the inventory screen and the item actions it animates:
// where the seven item models stand around the selected
// slot and how they swing while the selection moves, the grow/shrink of the equipped model, the
// shrink of a slot that an action empties, the examine glare's timing. The module decides the
// timing and the poses; the game rules of each action (equipping, reloading, healing, using an
// item on an event) and everything the screen keeps go through CarouselHost. Standard library
// only.

#pragma once

#include <array>
#include <cstdint>

#include "engine/inventory/inventory.h"
#include "engine/inventory/item_actions.h"

namespace sh::inventory {

constexpr int kCarouselModels = 7;    // displays 0..6: the item row
constexpr int kEquippedModel = 7;     // display 7: the item in hand

struct CarouselState {
    std::int32_t timer = 0;        // frames into a scroll (signed while scrolling right) or an action
    std::int32_t shown_slot = 0;   // the slot the carousel is centred on
    std::int32_t glare_time = 0;   // the examine glare, 0..64
    std::int32_t ammo_slot = -1;   // the ammo slot the last reload took rounds from
    // A finger turns the ring (src/input/inventory_pointer.h): the row is drawn centred on `finger_position` (Q12
    // slots, 0 .. slot count) instead of running its own scroll.
    bool finger = false;
    std::int32_t finger_position = 0;
};

class CarouselHost {
public:
    virtual ~CarouselHost() = default;

    virtual std::int32_t step() = 0;
    virtual std::int32_t selected_slot() = 0;
    virtual std::int32_t slot_count() = 0;      // slots the screen shows
    virtual std::int32_t display_slot(int display) = 0;  // the slot a model shows, -1 none
    virtual Slot slot(std::int32_t index) = 0;
    virtual std::int32_t weapon_slot() = 0;     // slot of the item in hand

    // Model poses (Q8 position, Q12 scale, Q12 angle around the vertical axis).
    virtual void scale_model(int display, std::int32_t scale) = 0;
    virtual void place_model(int display, std::int32_t x, std::int32_t y, std::int32_t z) = 0;
    virtual void turn_model(int display, std::int32_t angle) = 0;
    virtual void spin_model(int display, std::int32_t delta) = 0;

    // Screen state.
    virtual void set_step(std::int32_t step, bool reset_substep) = 0;
    virtual void select_item_row() = 0;
    virtual void reset_selection_age() = 0;
    virtual bool files_loaded() = 0;
    virtual bool confirm_clicked() = 0;  // the confirm or cancel button

    // Slots.
    virtual void set_slot_count(std::int32_t index, std::int32_t count) = 0;
    virtual void empty_slot(std::int32_t index) = 0;
    virtual void refresh_commands() = 0;

    // Game rules of the actions.
    virtual bool is_ammo(std::int32_t index) = 0;
    virtual bool used_up_on_use(std::int32_t index) = 0;  // the item goes once used on an event
    virtual void equip_start() = 0;     // first frame of equipping the selected slot
    virtual void equip_finish() = 0;    // the selected slot is now in hand
    virtual void unequip_finish() = 0;  // nothing is in hand
    virtual std::int32_t reload_equipped() = 0;  // returns the ammo slot used, -1 none
    virtual ReloadResult reload_selected() = 0;
    virtual void use_health() = 0;      // a health item in the selected slot is used
};

// One frame of the carousel (the game's Inventory_PlayerItemScroll).
void update_carousel(CarouselState& state, CarouselHost& host);

// The items scrolled one slot (`right`: to the right, as the menu's scroll direction 1) and slot
// `entering` comes into view: the display showing slot entering - 7 (right) or entering + 7
// (left), modulo `slot_count` (which must be positive), now shows `entering`. Returns that
// display; display 0 when none showed that slot.
int scroll_in_slot(std::array<std::int32_t, kCarouselModels>& displays, bool right, std::int32_t entering,
                   std::int32_t slot_count);

// --- The results screen's six item models (the game's Gfx_Results_ItemsPosition).

constexpr int kResultsModels = 6;

struct ResultsPose {
    std::int32_t x = 0, y = 0, z = 0;  // Q8
};

constexpr std::int32_t kResultsTilt = 512;  // Q12 angle: 45 degrees around x and z
constexpr std::int32_t kResultsSpin = 8;    // Q12 angle added around y each frame

// Where each of the six results models stands (a hexagon in front of the camera).
const std::array<ResultsPose, kResultsModels>& results_poses();

}  // namespace sh::inventory
