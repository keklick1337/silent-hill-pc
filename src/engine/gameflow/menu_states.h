#pragma once

// The in-game states that hand the screen to a menu or a movie: the pause, the
// way into the options, item and paper map screens, the event movie and the save prompt. Each state stops the world
// or fades out, loads what its screen needs and switches the game state to that screen; the screens themselves are
// other modules. The host owns the game objects (fades, files, sounds, the pause menu, the movie player).
//
// Contract: with the same inputs the calls on the host equal the original states', in the same order.

#include <cstdint>

#include "engine/gameflow/event_steps.h"
#include "engine/gameflow/game_states.h"

namespace sh::gameflow {

// Game data the states need (numbers of the game's sound driver, messages and flags).
struct MenuStateRules {
    int pause_on_sound = 0;            // sound driver command played when the pause starts
    int pause_off_sound = 0;           // ... and when it ends
    int stop_stream_sound = 0;         // stops streamed audio before the options screen loads
    int no_map_message = 0;
    int too_dark_message = 0;
    int save_prompt_message = 0;
    std::int16_t save_seen_flag = 0;   // the save prompt was answered once: later saves do not ask
    int save_without_prompt = 0;       // save point parameter that saves without asking (besides 0)
    std::uint8_t movie_resumes_game = 0;  // event option: after the movie the game resumes without its fade in
};

// The screen game states the menu states hand over to.
enum class MenuScreen : std::uint8_t { Options, Inventory, PaperMap, Save, InGame };

// Why the paper map cannot open.
enum class MapBlock : std::uint8_t { None, NoMap, TooDark };

class MenuStateHost : public StepHost {
public:
    virtual void enter_state(GameState s) { sh::game::session().set_sys_state_next(static_cast<std::int32_t>(s)); }
    virtual RunningEvent running_event() const = 0;
    virtual void set_flag(std::int16_t flag) = 0;

    // The world is frozen this frame and the last gameplay frame stays on screen.
    virtual void hold_world() = 0;
    // The frozen frame is let go when the next frame is presented (this frame's world draw is already off).
    virtual void release_world() = 0;
    // The fade is done and the queued files are in (the game's "screen ready" signal of the frame).
    virtual bool screen_ready() const = 0;
    virtual void clear_screen_ready() = 0;
    virtual bool chunks_idle() = 0;                // the map chunk loader has nothing in flight
    virtual void sound_command(int command) = 0;
    virtual void fade_out(bool restart) = 0;       // restart = from the beginning, else a running fade goes on
    virtual void fade_in() = 0;
    virtual void fade_instant() = 0;               // the fade started this frame completes at once
    virtual void open_screen(MenuScreen screen) = 0;  // switches the game state

    // Pause.
    virtual bool native_pause_menu() const = 0;    // the PC pause menu replaces the blinking "PAUSED"
    virtual void draw_paused() = 0;
    virtual void paused_extras() = 0;              // the original's (empty) hook and the clear-time clock
    virtual bool pause_pressed() const = 0;
    virtual bool debug_save_combo() const = 0;
    virtual void clear_event_param() = 0;
    virtual void reset_pause_menu() = 0;
    virtual void update_pause_menu() = 0;
    virtual bool pause_menu_return_pending() const = 0;  // the options screen was opened from the pause menu

    // Options and inventory.
    virtual void load_options_screen() = 0;

    // Paper map.
    virtual MapBlock map_block() const = 0;
    virtual bool map_pressed() const = 0;
    virtual void load_paper_map() = 0;

    // Movie.
    virtual void load_movie_stream() = 0;
    virtual void play_movie(int param) = 0;        // blocks until the movie ends or is skipped
    virtual void resume_game_at_step(int step) { sh::game::session().game.game_steps[0] = step; }

    // Save.
    virtual void poll_saves() = 0;
    virtual void store_player() = 0;               // the save point and the player into the savegame
    virtual int save_location() const = 0;         // the save point the savegame now names
    virtual void load_save_screen() = 0;
    virtual void stop_rumble() = 0;
};

class MenuStates {
public:
    void set_rules(const MenuStateRules& r) { rules_ = r; }

    // Runs menu state `s` (Paused, OptionsMenu, StatusMenu, MapScreen, Movie, SaveMenu, SaveMenuAlt).
    void run(GameState s, MenuStateHost& host);

    void paused(MenuStateHost& host);
    void options(MenuStateHost& host);
    void status(MenuStateHost& host);
    void map_screen(MenuStateHost& host);
    void movie(MenuStateHost& host);
    void save(MenuStateHost& host);

    std::int32_t blink() const { return blink_; }

private:
    MenuStateRules rules_{};
    std::int32_t blink_ = 0;  // the "PAUSED" text blinks on this clock (Q12 seconds)
};

}  // namespace sh::gameflow
