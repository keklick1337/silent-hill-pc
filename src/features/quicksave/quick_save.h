// SPDX-License-Identifier: GPL-3.0-or-later
//
// Quick save and quick load: the keys key_quicksave / key_quickload (F6 / F8),
// the controller buttons pad_quicksave / pad_quickload, the console's `quicksave` / `quickload` and the pause menu.
//
// quick_save_instant = 0: the save key opens the save screen, and only with save_anywhere = 1 (GAMEPLAY CHANGE:
// the original saves at notebooks only); the load key opens the load screen.
// quick_save_instant = 1 (GAMEPLAY CHANGE): the save key writes the quick slot at once, the load key loads it at
// once through the load screen's own start, so the reset, the map load and the fade are those of a load from the
// list.
#pragma once

namespace sh::quicksave {

// True while a living character of a boss type (character.json "is_boss") is on the map. A save taken then would
// put the player back into a fight they may have entered without health or ammunition, with no way out, which is
// why the original has no save point in a boss room either. Asking the characters instead of the room covers
// every boss and lifts by itself when the boss dies. Loading is never refused for it.
bool boss_alive();

// The original save and load screens, opened the way the notebook opens them.
void open_save_screen();
void open_load_screen();

// Saves to / loads from the quick slot at once. `from_console`: the console runs it, so its open input line does
// not count against free play. False when refused: quick_save_instant is off, the game is not in free play, a
// boss is alive, a script handler vetoed it, there is no usable quick save. The reason is logged, and shown to the
// player where they can act on it.
bool save_now(bool from_console);
bool load_now(bool from_console);

// Once a frame: reads the keys and buttons and acts on a new press.
void poll_keys();

enum class Request { None, Save, Load };

// Turns the held state of the two keys into presses. The controller's buttons arrive as presses already.
class PressDetector {
public:
    struct Input {
        bool save_key_down = false;
        bool load_key_down = false;
        bool pad_save_pressed = false;
        bool pad_load_pressed = false;
        // Ctrl+<key> belongs to the renderer's diagnostics, which read the same keys: nothing is requested then.
        bool ctrl_down = false;
    };

    // Save wins when both arrive in one frame. A key held while Ctrl is released does not count as a new press.
    Request update(const Input& input) noexcept;

private:
    bool save_was_down_ = false;
    bool load_was_down_ = false;
};

}  // namespace sh::quicksave
