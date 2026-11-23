// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/inventory/inventory_menu.h"

#include <algorithm>

namespace sh::inventory {

namespace {

// The selection animation runs this many frames; input is taken once it is over (the item
// row already accepts scrolling halfway).
constexpr std::int32_t kSettledAge = 8;
constexpr std::int32_t kItemsReadyAge = 4;

void select(MenuState& state, Selection selection)
{
    state.selection_age = 1;
    state.selection = selection;
}

void start_step(MenuState& state, ScreenStep step) { state.step = static_cast<std::int32_t>(step); }

bool at_step(const MenuState& state, ScreenStep step) { return state.step == static_cast<std::int32_t>(step); }

void leave(MenuHost& host, MenuSound sound, int pan, MenuExit exit)
{
    host.sound(sound, pan);
    host.leave(exit);
}

void update_items(MenuState& state, MenuHost& host, const MenuInput& in)
{
    const bool settled = state.selection_age == kSettledAge;
    if ((in.left_clicked && settled) || (in.left_held && (state.scrolling || settled))) {
        state.scrolling = true;
        state.scrolled = true;
        state.selection_age = 2;
        state.command = 0;
        host.sound(MenuSound::Move, -64);
        const int count = host.slot_count();
        state.selected_slot = (state.selected_slot + count - 1) % count;
        host.scroll(0, (state.selected_slot + count - 3) % count);
    } else if (((in.right_clicked || in.right_pulsed) && settled) ||
               (in.right_held && (state.scrolling || settled))) {
        if (in.right_clicked || in.right_held)
            state.scrolling = true;
        state.scrolled = true;
        state.selection_age = 2;
        state.command = 0;
        host.sound(MenuSound::Move, 64);
        const int count = host.slot_count();
        state.selected_slot = (state.selected_slot + 1) % count;
        host.scroll(1, (state.selected_slot + 3) % count);
    } else if (in.up_clicked && settled) {
        state.selection_age = 1;
        if (host.weapon_in_hand()) {
            state.selection = Selection::EquippedItem;
            host.sound(MenuSound::Move, 0);
        }
    } else if ((in.cancel || in.down_clicked) && settled) {
        state.selection_age = 1;
        host.sound(in.down_clicked ? MenuSound::Move : MenuSound::Cancel, 0);
        state.selection = Selection::Exit;
    } else if (in.enter && state.selection_age >= kSettledAge) {
        const Slot slot = host.slot(state.selected_slot);
        if (host.selection_refused(state.selected_slot)) {
            host.sound(MenuSound::Error, 64);
        } else if (slot.command == static_cast<std::uint8_t>(Command::Unusable)) {
            start_step(state, ScreenStep::CantUseHere);
            host.sound(MenuSound::Error, 64);
        } else {
            state.selection_age = 1;
            state.command = 0;
            if (slot.command != static_cast<std::uint8_t>(Command::None)) {
                state.selection = Selection::ItemCommands;
                host.sound(MenuSound::Confirm, 64);
            }
        }
    } else if (!in.item_button) {
        state.scrolling = false;
    } else {
        leave(host, MenuSound::Cancel, 0, MenuExit::Game);
    }
}

void update_equipped(MenuState& state, MenuHost& host, const MenuInput& in)
{
    if (in.down_clicked) {
        state.selection_age = 1;
        host.sound(MenuSound::Move, 0);
        state.selection = Selection::Items;
    } else if (in.enter) {
        state.selection_age = 1;
        state.command = 0;
        if (host.slot(host.weapon_slot()).command != static_cast<std::uint8_t>(Command::None)) {
            state.selection = Selection::EquippedCommands;
            host.sound(MenuSound::Confirm, 0);
        }
    } else if (in.cancel) {
        select(state, Selection::Exit);
        host.sound(MenuSound::Cancel, 0);
    } else if (in.item_button) {
        leave(host, MenuSound::Cancel, 0, MenuExit::Game);
    }
}

void update_exit(MenuState& state, MenuHost& host, const MenuInput& in)
{
    if (in.up_clicked) {
        state.selection_age = 1;
        host.sound(MenuSound::Move, 0);
        state.selection = Selection::Items;
    } else if (in.left_clicked) {
        state.selection_age = 1;
        host.sound(MenuSound::Move, -64);
        state.selection = Selection::Option;
    } else if (in.right_clicked) {
        state.selection_age = 1;
        host.sound(MenuSound::Move, 64);
        state.selection = Selection::Map;
    } else if (in.item_button || in.enter || in.cancel) {
        leave(host, MenuSound::Cancel, 0, MenuExit::Game);
    }
}

// The Option and Map buttons: up goes to the items, the direction toward Exit or cancel to
// Exit.
bool update_side_button(MenuState& state, MenuHost& host, const MenuInput& in, bool toward_exit)
{
    if (in.up_clicked) {
        state.selection_age = 1;
        host.sound(MenuSound::Move, 0);
        state.selection = Selection::Items;
        return true;
    }
    if (toward_exit || in.cancel) {
        state.selection_age = 1;
        host.sound(!toward_exit || in.cancel ? MenuSound::Cancel : MenuSound::Move, 0);
        state.selection = Selection::Exit;
        return true;
    }
    return false;
}

void update_option(MenuState& state, MenuHost& host, const MenuInput& in)
{
    if (update_side_button(state, host, in, in.right_clicked))
        return;
    if (in.enter)
        leave(host, MenuSound::Confirm, -64, MenuExit::Options);
    else if (in.item_button)
        leave(host, MenuSound::Cancel, 0, MenuExit::Game);
}

void update_map(MenuState& state, MenuHost& host, const MenuInput& in)
{
    if (!update_side_button(state, host, in, in.left_clicked)) {
        if (in.enter) {
            if (host.can_see() && host.has_map())
                leave(host, MenuSound::Confirm, 64, MenuExit::Map);
            else
                host.sound(MenuSound::Error, 64);
        } else if (in.item_button) {
            leave(host, MenuSound::Cancel, 0, MenuExit::Game);
        }
    }

    if (!host.has_map())
        host.show_map_unavailable(true);
    else if (!host.can_see())
        host.show_map_unavailable(false);
}

void examine_or_too_dark(MenuState& state, MenuHost& host, int slot)
{
    if (host.can_see()) {
        select(state, Selection::Examine);
        start_step(state, ScreenStep::Examine);
        host.examine(slot);
    } else {
        start_step(state, ScreenStep::TooDark);
    }
}

void start_reload(MenuState& state, MenuHost& host, int slot)
{
    start_step(state, slot != host.weapon_slot() ? ScreenStep::ReloadOther : ScreenStep::ReloadEquipped);
}

void run_command(MenuState& state, MenuHost& host, int slot)
{
    state.selection_age = 1;
    switch (static_cast<Command>(host.slot(slot).command)) {
        case Command::OnOff:
            select(state, Selection::Items);
            host.toggle(slot, state.command == 0);
            start_step(state, ScreenStep::Browse);
            break;

        case Command::UseLook:
            if (state.command != 0)
                examine_or_too_dark(state, host, slot);
            else if (host.item_use_allowed(slot))
                host.use_item(slot);
            break;

        case Command::Look:
            examine_or_too_dark(state, host, slot);
            break;

        case Command::UseHealth:
            if (!host.item_use_allowed(slot))
                break;
            select(state, Selection::Health);
            start_step(state, ScreenStep::UseHealth);
            break;

        case Command::Use:
            if (host.item_use_allowed(slot))
                host.use_item(slot);
            break;

        case Command::Equip:
            select(state, Selection::EquippedItem);
            start_step(state, ScreenStep::Equip);
            break;

        case Command::Unequip:
            select(state, Selection::EquippedItem);
            start_step(state, ScreenStep::Unequip);
            break;

        case Command::Reload:
            start_reload(state, host, slot);
            break;

        case Command::EquipReload:
        case Command::UnequipReload: {
            const bool equip = static_cast<Command>(host.slot(slot).command) == Command::EquipReload;
            if (state.command == 0) {
                state.selection = Selection::EquippedItem;
                start_step(state, equip ? ScreenStep::Equip : ScreenStep::Unequip);
            } else {
                start_reload(state, host, slot);
            }
            break;
        }

        case Command::Unusable:
        case Command::None:
            break;
    }
    host.sound(at_step(state, ScreenStep::CantUseHere) ? MenuSound::Error : MenuSound::Confirm, 64);
}

void update_commands(MenuState& state, MenuHost& host, const MenuInput& in)
{
    const int slot = state.selection == Selection::ItemCommands ? state.selected_slot : host.weapon_slot();
    const int rows = command_rows(host.slot(slot).command);
    if (in.down_pulsed) {
        if (state.command < rows - 1) {
            state.selection_age = 1;
            state.command++;
            host.sound(MenuSound::Move, 64);
        }
    } else if (in.up_pulsed) {
        if (state.command > 0) {
            state.selection_age = 1;
            state.command--;
            host.sound(MenuSound::Move, 64);
        }
    } else if (in.enter) {
        run_command(state, host, slot);
    } else if (in.cancel) {
        state.selection_age = 1;
        state.command = 0;
        state.selection = Selection::Items;
        host.sound(MenuSound::Cancel, 0);
    }
}

}  // namespace

int command_rows(std::uint8_t command)
{
    switch (static_cast<Command>(command)) {
        case Command::UseHealth:
        case Command::Use:
        case Command::Equip:
        case Command::Unequip:
        case Command::Reload:
        case Command::Look:
            return 1;
        case Command::EquipReload:
        case Command::UnequipReload:
        case Command::OnOff:
        case Command::UseLook:
            return 2;
        default:
            return 0;
    }
}

void update_menu(MenuState& state, MenuHost& host)
{
    const MenuInput in = host.read_input();

    state.selection_age++;
    if (state.selection_age >= kSettledAge)
        state.previous = state.selection;
    state.selection_age = std::clamp(state.selection_age, 0, kSettledAge);

    if (!at_step(state, ScreenStep::Browse))
        return;
    const std::int32_t ready = state.selection == Selection::Items ? kItemsReadyAge : kSettledAge;
    if (state.selection_age < ready)
        return;

    switch (state.selection) {
        case Selection::Items:
            update_items(state, host, in);
            break;
        case Selection::EquippedItem:
            update_equipped(state, host, in);
            break;
        case Selection::Exit:
            update_exit(state, host, in);
            break;
        case Selection::Option:
            update_option(state, host, in);
            break;
        case Selection::Map:
            update_map(state, host, in);
            break;
        case Selection::ItemCommands:
        case Selection::EquippedCommands:
            update_commands(state, host, in);
            break;
        case Selection::Examine:
            state.selection_age = 1;
            break;
        case Selection::Health:
            break;
    }
}

}  // namespace sh::inventory
