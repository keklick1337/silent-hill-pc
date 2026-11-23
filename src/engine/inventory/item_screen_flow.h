// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The item screen as a game state: opening the
// inventory, running the menu while the item carousel is at rest, the notices, the fades out
// to the Option screen, the paper map and back into the game, and the results screen that
// follows a finished game (save or start over). The module owns the order of the steps; every
// step's work is done by ScreenHost. Standard library only.

#pragma once

#include <cstdint>

#include "engine/inventory/inventory_menu.h"

namespace sh::inventory {

// Steps of the item screen besides the menu's (ScreenStep).
enum class FlowStep : std::int32_t {
    Open = 0,
    LeaveWithWeapon = 17,
    ToOptions = 18,
    ToMap = 19,
    Closing = 20,
    ResultsOpen = 21,
    ResultsWait = 22,
    ResultsChoice = 23,
    ResultsToSave = 24,
    ResultsRestart = 25,
};

// The screen the inventory is entered from, when it decides the first selection.
enum class EnteredFrom : std::uint8_t { Other, StatusScreen, Options, Map };

enum class Notice : std::uint8_t { CantUseHere, TooDark };

class ScreenHost {
public:
    virtual ~ScreenHost() = default;

    virtual std::int32_t step() = 0;
    virtual void set_step(std::int32_t step) = 0;  // also restarts the step's own sequence
    virtual std::int32_t sub_step() = 0;
    virtual void set_sub_step(std::int32_t sub_step) = 0;

    virtual void begin_frame() = 0;        // text colour, music
    virtual void advance_play_time() = 0;

    // Opening.
    virtual bool results_pending() = 0;    // a finished game waits for its results screen
    virtual bool restart_requested() = 0;  // the save screen asked to start over
    virtual void restart_game() = 0;
    virtual void grant_unlocked_items() = 0;
    virtual void open_screen() = 0;
    virtual EnteredFrom entered_from() = 0;
    // Resets the menu; `selection` set: the menu starts there.
    virtual void start_menu(bool has_selection, Selection selection) = 0;
    virtual void prepare_items() = 0;

    // Browsing.
    virtual bool menu_ready() = 0;  // the carousel shows the selected slot, no fade runs
    virtual void run_menu() = 0;
    virtual void draw_commands() = 0;
    virtual void show_notice(Notice notice) = 0;

    // Leaving.
    virtual void fade_out() = 0;
    virtual bool fade_finished() = 0;
    virtual void update_weapon() = 0;
    virtual void go_to_options() = 0;
    virtual void go_to_map() = 0;
    virtual void return_to_game() = 0;

    // Results screen.
    virtual void open_results() = 0;
    virtual bool results_continue_pressed() = 0;
    virtual bool results_side_pressed() = 0;
    virtual bool results_enter_pressed() = 0;
    virtual int results_choice() = 0;  // 0: save, 1: start over
    virtual void set_results_choice(int choice) = 0;
    virtual void sound(MenuSound sound, int pan) = 0;
    virtual void load_save_screen() = 0;
    virtual void go_to_save_screen() = 0;
    virtual bool restart_data_loaded() = 0;

    // Drawing at the end of the frame.
    virtual void draw_screen() = 0;
    virtual void draw_scroll_arrows() = 0;
    virtual void draw_results() = 0;
};

// One frame of the item screen (the game's GameState_ItemScreens_Update).
void update_item_screen(ScreenHost& host);

// The play time of the save: Q12 seconds, unsigned, with up to three overflows counted.
void advance_play_time(std::uint32_t& timer, std::uint8_t& overflows, std::int32_t delta);

}  // namespace sh::inventory
