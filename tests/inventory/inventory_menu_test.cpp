// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the inventory screen menu (sh::inventory::update_menu). The fake host
// records every effect in order.

#include "engine/inventory/inventory_menu.h"

#include <string>
#include <vector>

#include "framework/sh_test.h"

namespace {

using namespace sh::inventory;

class FakeHost final : public MenuHost {
public:
    MenuInput input;
    Slots slots{};
    int count = 8;
    bool in_hand = true;
    int weapon = 1;
    bool refused = false;
    bool see = true;
    bool map = true;
    std::vector<std::string> log;

    MenuInput read_input() override { return input; }
    void sound(MenuSound s, int pan) override { log.push_back("sound " + std::to_string(int(s)) + " " + std::to_string(pan)); }
    int slot_count() override { return count; }
    Slot slot(int index) override { return slots[index]; }
    bool weapon_in_hand() override { return in_hand; }
    int weapon_slot() override { return weapon; }
    bool selection_refused(int) override { return refused; }
    bool can_see() override { return see; }
    bool has_map() override { return map; }
    void scroll(int d, int s) override { log.push_back("scroll " + std::to_string(d) + " " + std::to_string(s)); }
    void leave(MenuExit e) override { log.push_back("leave " + std::to_string(int(e))); }
    void show_map_unavailable(bool no_map) override { log.push_back(no_map ? "nomap" : "dark"); }
    void toggle(int s, bool on) override { log.push_back("toggle " + std::to_string(s) + (on ? " on" : " off")); }
    void examine(int s) override { log.push_back("examine " + std::to_string(s)); }
    void use_item(int s) override { log.push_back("use " + std::to_string(s)); }
};

MenuState browsing(Selection selection)
{
    MenuState state;
    state.selection = selection;
    state.selection_age = 7;  // becomes 8 this frame
    state.step = static_cast<int>(ScreenStep::Browse);
    return state;
}

std::uint8_t cmd(Command c) { return static_cast<std::uint8_t>(c); }

}  // namespace

SH_TEST(inventory_menu_waits_for_the_selection_animation) {
    FakeHost host;
    host.input.enter = true;
    MenuState state = browsing(Selection::Exit);
    state.selection_age = 5;
    update_menu(state, host);
    SH_CHECK(state.selection_age == 6 && host.log.empty());
    state.selection_age = 20;  // clamped to 8, the previous selection catches up
    state.previous = Selection::Items;
    update_menu(state, host);
    SH_CHECK(state.selection_age == 8 && state.previous == Selection::Exit);
    SH_CHECK(host.log.size() == 2 && host.log[1] == "leave 0");
}

SH_TEST(inventory_menu_scrolls_left_and_right) {
    FakeHost host;
    host.input.left_clicked = true;
    MenuState state = browsing(Selection::Items);
    state.selected_slot = 0;
    update_menu(state, host);
    SH_CHECK(state.selected_slot == 7 && state.selection_age == 2 && state.scrolling && state.scrolled);
    SH_CHECK(host.log.size() == 2 && host.log[0] == "sound 0 -64" && host.log[1] == "scroll 0 4");
    host.log.clear();
    host.input = {};
    host.input.right_pulsed = true;
    state.selection_age = 7;
    state.scrolling = false;
    update_menu(state, host);
    SH_CHECK(state.selected_slot == 0 && !state.scrolling);  // a pulse alone does not latch scrolling
    SH_CHECK(host.log.size() == 2 && host.log[1] == "scroll 1 3");
    // Holding keeps scrolling halfway through the animation once latched.
    host.input = {};
    host.input.right_held = true;
    state.scrolling = true;
    state.selection_age = 3;
    update_menu(state, host);
    SH_CHECK(state.selected_slot == 1);
    // Nothing pressed: the latch is released.
    host.input = {};
    state.selection_age = 7;
    update_menu(state, host);
    SH_CHECK(!state.scrolling);
}

SH_TEST(inventory_menu_opens_commands_and_refuses) {
    FakeHost host;
    host.slots[0] = {10, 1, cmd(Command::UseHealth), 0};
    host.input.enter = true;
    MenuState state = browsing(Selection::Items);
    update_menu(state, host);
    SH_CHECK(state.selection == Selection::ItemCommands && state.command == 0);
    host.refused = true;
    state = browsing(Selection::Items);
    host.log.clear();
    update_menu(state, host);
    SH_CHECK(state.selection == Selection::Items && host.log.back() == "sound 3 64");
    host.refused = false;
    host.slots[0].command = cmd(Command::Unusable);
    state = browsing(Selection::Items);
    update_menu(state, host);
    SH_CHECK(state.step == static_cast<int>(ScreenStep::CantUseHere));
}

SH_TEST(inventory_menu_runs_commands) {
    FakeHost host;
    host.slots[0] = {10, 1, cmd(Command::EquipReload), 0};
    host.weapon = 0;
    MenuState state = browsing(Selection::ItemCommands);
    host.input.down_pulsed = true;
    update_menu(state, host);
    SH_CHECK(state.command == 1);
    host.input = {};
    host.input.enter = true;
    state.selection_age = 7;
    update_menu(state, host);
    SH_CHECK(state.step == static_cast<int>(ScreenStep::ReloadEquipped) && host.log.back() == "sound 1 64");
    host.slots[0].command = cmd(Command::Look);
    host.see = false;
    state = browsing(Selection::ItemCommands);
    update_menu(state, host);
    SH_CHECK(state.step == static_cast<int>(ScreenStep::TooDark) && state.selection == Selection::ItemCommands);
    host.see = true;
    state = browsing(Selection::ItemCommands);
    update_menu(state, host);
    SH_CHECK(state.step == static_cast<int>(ScreenStep::Examine) && host.log.back() == "sound 1 64");
    SH_CHECK(host.log[host.log.size() - 2] == "examine 0");
    host.slots[0].command = cmd(Command::OnOff);
    state = browsing(Selection::ItemCommands);
    state.command = 1;
    update_menu(state, host);
    SH_CHECK(state.selection == Selection::Items && host.log[host.log.size() - 2] == "toggle 0 off");
}

SH_TEST(inventory_menu_map_button) {
    FakeHost host;
    host.map = false;
    host.input.enter = true;
    MenuState state = browsing(Selection::Map);
    update_menu(state, host);
    SH_CHECK(host.log.size() == 2 && host.log[0] == "sound 3 64" && host.log[1] == "nomap");
    host.log.clear();
    host.map = true;
    state = browsing(Selection::Map);
    update_menu(state, host);
    SH_CHECK(host.log.size() == 2 && host.log[0] == "sound 1 64" && host.log[1] == "leave 2");
    SH_CHECK(command_rows(cmd(Command::UseLook)) == 2 && command_rows(cmd(Command::Unusable)) == 0);
}
