// SPDX-License-Identifier: GPL-3.0-or-later
// The pause menu of the game in play: Continue, Save, Load, Controls, Option, Exit (and Debug Options once the
// player turns them on) over the frozen frame, in place of the original's blinking PAUSED. The rows are
// gamedata/menus/pause.lua; this is the game's side of them: when the menu stands in for the original pause, what
// each row does, the fades to the screens it opens and the way back from them. The pause state of the game drives
// it (update), the main loop asks it where the game is.
#pragma once

#include "engine/menu/menu_model.h"

namespace sh::ui {

// Why Save is greyed out. Loading is never the problem: it is offered even out of a boss fight, as quick load is.
enum class SaveBlock {
    None,
    BossFight,
    // The menu came up from Back without `pause_menu` or `save_anywhere`: saving stays at the notebooks, as in
    // the original.
    SavePointsOnly,
};

constexpr SaveBlock save_block(bool boss_fight, bool pause_menu_option, bool save_anywhere) {
    if (boss_fight) {
        return SaveBlock::BossFight;
    }
    return pause_menu_option || save_anywhere ? SaveBlock::None : SaveBlock::SavePointsOnly;
}

constexpr bool save_allowed(SaveBlock block) { return block == SaveBlock::None; }

// The line the console prints when a greyed Save is picked; nullptr when nothing blocks it.
constexpr const char* save_block_notice(SaveBlock block) {
    return block == SaveBlock::SavePointsOnly ? "Save at a notebook (save_anywhere = 1 allows saving here)"
           : block == SaveBlock::BossFight    ? "Can't save during a boss fight"
                                              : nullptr;
}

class PauseMenu {
public:
    // Play is going into the pause on its own pause press: notes whether that press was Back (Escape), which
    // brings this menu up when `pause_menu_on_back` is set.
    void note_entry();
    // This pause is the menu (`pause_menu`, Back with `pause_menu_on_back`, or the debug menu, which lives in the
    // pause); false: the original PAUSED.
    bool replaces_pause() const;
    // The clear-time clock stands still (`pause_menu`, or the debug menu).
    bool stops_clock() const;

    // The whole frame of the paused game while the menu replaces the pause.
    void update();
    // Up, and neither fading out nor away on a screen it opened.
    bool is_shown() const;

    // Away on the Save, Load or Option screen, to come back to the pause when it closes.
    bool return_pending() const { return return_pending_; }
    // The same, once: the answer is forgotten.
    bool take_return();
    // The menu is over, whichever way: no pending return, no frozen frame held, the pause's mute lifted when it
    // was still held for a screen.
    void reset();
    // Once a tick, after the game's own update: keeps the cues of a screen the pause opened audible under the
    // held mute, and resets when the game has gone anywhere but that screen or back.
    void away_update();

    // F10 in play: into the pause as the pause button would go, with the debug menu to open on its first tick.
    void enter_for_debug();

private:
    // What a row asked for; carried out after the menu's update, with its cue.
    enum class Request { None, Resume, Debug, Save, Load, Option, Exit };
    // Where the menu leaves to once its fade is black.
    enum class Leaving { Nowhere, SaveScreen, LoadScreen, OptionScreen, Title };

    void register_rows();
    void open();
    void draw();
    sh::menu::MenuInput read_input(bool first_frame);
    void resume(int cue);
    void leave(Leaving to);
    void start_fade(Request request);

    bool rows_registered_ = false;
    bool open_ = false;
    bool first_frame_ = false;    // the tick after opening: the pointer's motion is the mouse-look letting go
    bool entered_by_back_ = false;  // kept through the screens the menu opens, until the next pause from play
    bool return_pending_ = false;
    int blink_ = 0;                 // the heading's blink clock, in the game's raw time
    SaveBlock block_ = SaveBlock::None;
    Request request_ = Request::None;
    Leaving leaving_ = Leaving::Nowhere;
};

PauseMenu& pause_menu();

}  // namespace sh::ui
