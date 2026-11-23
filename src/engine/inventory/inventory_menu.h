// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The inventory screen's menu: which element is selected —
// the item row, the equipped item, the three bottom buttons (Option, Exit, Map), a command
// list — and how input moves the selection, scrolls the items, opens command lists and starts
// commands. The module decides; everything with an effect outside the menu (sounds, scrolling
// the item models, leaving the screen, the flashlight, item use, examine pictures) goes through
// MenuHost in the order the game does it. Standard library only.

#pragma once

#include <cstdint>

#include "engine/inventory/inventory.h"

namespace sh::inventory {

// The selected element (the save of the screen keeps the value).
enum class Selection : std::uint32_t {
    Items = 0,
    EquippedItem = 1,
    Exit = 2,
    Option = 3,
    Map = 4,
    ItemCommands = 5,
    EquippedCommands = 6,
    Health = 7,   // a health item is being used
    Examine = 8,  // a picture of an item is shown
};

// Steps of the item screen the menu reads or starts (the game's state step).
enum class ScreenStep : std::int32_t {
    Browse = 1,
    Equip = 5,
    Unequip = 6,
    ReloadEquipped = 7,
    ReloadOther = 8,
    UseHealth = 9,
    UseOnEvent = 11,
    CantUseHere = 12,
    Examine = 13,
    ExamineShown = 14,
    ExamineClosing = 15,
    TooDark = 16,
    Closing = 20,
};

enum class MenuSound : std::uint8_t { Move, Confirm, Cancel, Error };

// Where the menu leaves the screen to.
enum class MenuExit : std::uint8_t { Game, Options, Map };

// The menu state the rest of the screen reads.
struct MenuState {
    Selection selection = Selection::Items;
    Selection previous = Selection::Items;  // the selection the frame animation started from
    std::int32_t selection_age = 0;         // frames since the selection changed, held at 8
    std::int32_t command = 0;               // highlighted row of a command list
    bool scrolling = false;                 // a held direction keeps scrolling the items
    bool scrolled = false;                  // the items moved since the description was drawn
    std::int32_t selected_slot = 0;
    std::int32_t step = 0;                  // a ScreenStep
};

// Input of one frame.
struct MenuInput {
    bool up_clicked = false;
    bool up_pulsed = false;
    bool down_clicked = false;
    bool down_pulsed = false;
    bool left_clicked = false;
    bool left_held = false;
    bool right_clicked = false;
    bool right_held = false;
    bool right_pulsed = false;
    bool enter = false;
    bool cancel = false;
    bool item_button = false;  // the button that opened the inventory closes it
};

class MenuHost {
public:
    virtual ~MenuHost() = default;

    // Reads this frame's input. May change the state (the mouse picks elements directly).
    virtual MenuInput read_input() = 0;
    virtual void sound(MenuSound sound, int pan) = 0;

    virtual int slot_count() = 0;
    virtual Slot slot(int index) = 0;
    virtual bool weapon_in_hand() = 0;
    virtual int weapon_slot() = 0;

    // Game rules: an item that cannot be selected now; whether it is light enough to look at
    // things; whether the player has the map of this place.
    virtual bool selection_refused(int slot) = 0;
    virtual bool can_see() = 0;
    virtual bool has_map() = 0;

    // Effects. Each may change the state (the host owns where it lives).
    virtual void scroll(int direction, int slot) = 0;  // direction 0: left, 1: right
    virtual void leave(MenuExit exit) = 0;
    virtual void show_map_unavailable(bool no_map) = 0;
    virtual void toggle(int slot, bool on) = 0;
    virtual void examine(int slot) = 0;
    virtual void use_item(int slot) = 0;
    // Mod hook (item.use): false and the use command does nothing.
    virtual bool item_use_allowed(int slot) { (void)slot; return true; }
};

// One frame of the menu (the game's Inventory_Logic).
void update_menu(MenuState& state, MenuHost& host);

// Rows of the command list a command shows.
int command_rows(std::uint8_t command);

}  // namespace sh::inventory
