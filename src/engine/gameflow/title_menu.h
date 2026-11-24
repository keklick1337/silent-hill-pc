#pragma once

// The title menu game state: the entry list (load, continue, new game,
// options, exit) with its visibility rules, the selection and its wrap, the exit confirmation, the
// difficulty picker, the hand-off to the chosen game state and the idle timer
// that starts the attract demo or the intro movie. The entries, their labels, sounds and target states and the
// layout are data (gamedata/menu/title_menu.json); the host owns the game objects (pad, mouse, saves, fades,
// loading, text drawing).
//
// Contract: with the same inputs the calls on the host equal the original title code's, in the same order.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

#include "engine/game_state/session.h"

namespace sh::gameflow {

enum class TitleEntryKind : std::uint8_t { Load, Continue, NewGame, Options, Exit };

struct TitleEntry {
    TitleEntryKind kind = TitleEntryKind::Options;
    std::string label;          // menu text key (the English text; the host translates and draws it)
    int offset_x = -1;          // hand-measured half width of the untranslated label; -1 = measured
    std::string sound;          // sound played when the entry is picked
    std::string next;           // game state the hand-off goes to ("" = the entry never hands off)
    std::string next_alt;       // continue: the state when an autosave exists; new game: when the movie is skipped
};

struct TitleLayout {
    int column_x = 158;         // centre of the menu column
    int row_pitch = 20;
    int touch_row_pitch = 0;    // a touch screen's rows (taller to tap; still bottom-aligned); 0 = row_pitch
    int last_row_y = 264;       // rows are bottom-aligned: the last visible row is drawn here
    int hover_lead = 3;         // the mouse row starts this many pixels above the glyph top
    int difficulty_y = 204;     // first difficulty row
    std::string difficulty_align = "center";  // "center" | "left" (left = fixed difficulty_offset_x)
    int difficulty_offset_x = 34;  // half width of an untranslated difficulty label
};

struct TitleConfirm {
    std::string question;                // drawn on the row of `row_entry`
    std::string answers[2];              // yes, no
    int answer_spread = 40;              // answers sit this far left and right of the column centre
    TitleEntryKind row_entry = TitleEntryKind::Options;
};

struct TitleSounds {
    std::string move, cancel, start_game, open_confirm;
};

struct TitleMenuData {
    std::vector<TitleEntry> entries;     // at most 32
    TitleLayout layout;
    TitleConfirm confirm;
    TitleSounds sounds;
    int idle_frames = 1740;              // idle time on the title before the intro movie / demo starts
    int intro_every = 3;                 // every n-th idle timeout plays the intro movie, the others a demo
    std::string idle_state;              // game state of the intro movie

    static Result<TitleMenuData> parse(std::string_view json);
    int find(TitleEntryKind kind) const;  // entry index, -1 when absent
};

// Pad state as the title reads it: the menu directions repeat while held, the buttons are press edges.
struct TitlePad {
    bool up = false, down = false;
    bool enter = false, cancel = false;
    bool held_any = false;
};

// The mouse in the title's text space; `left`/`right` are click edges, false when the cursor is off screen.
struct TitleMouse {
    bool in_view = false;
    int x = 0, y = 0;
    bool moved = false, left = false, right = false;
};

// The input of the exit confirmation (the shared PC menu input, pad bits a mouse bind produces removed).
struct ConfirmInput {
    bool left = false, right = false, confirm = false, cancel = false;
    bool mouse_moved = false, mouse_left = false, mouse_right = false;
};

class TitleHost {
public:
    virtual ~TitleHost() = default;

    // The game state record.
    virtual int state_step() const { return sh::game::session().game.game_steps[0]; }
    virtual void set_state_step(int step) { sh::game::session().game.game_steps[0] = step; }
    // the game state is still the title menu
    virtual bool in_title() const { return sh::game::session().game_state() == sh::game::game_state::kMainMenu; }
    // writes the title game state (input cancels a pending change)
    virtual void stay_in_title() { sh::game::session().game.game_state = sh::game::game_state::kMainMenu; }
    // the demo boot reached the main load screen
    virtual bool demo_loaded() const { return sh::game::session().game_state() == sh::game::game_state::kMainLoadScreen; }
    virtual int idle_frames() const = 0;
    virtual void reset_idle() { sh::game::session().system.flow.frame_counters[1] = 0; }
    virtual int demo_count() const = 0;          // idle timeouts so far (demos and intro movies)
    virtual void set_demo_count(int count) = 0;

    // Settings and saves.
    virtual bool skip_to_game() const = 0;       // start a new game on the default difficulty at once
    virtual bool attract_demos() const = 0;
    virtual bool autosave_alive() const = 0;
    virtual void poll_saves() = 0;
    virtual int save_count() const = 0;

    // Input.
    virtual TitlePad pad() const = 0;
    virtual TitleMouse mouse() const = 0;
    virtual ConfirmInput confirm_input() const = 0;
    virtual void press_enter() = 0;              // a mouse click acts as the confirm button
    virtual void press_cancel() = 0;
    virtual void release_enter() = 0;
    virtual void consume_pad() = 0;              // the confirmation box ate this frame's presses

    // Actions.
    virtual void request_demo() { sh::game::session().system.process.process_flags = sh::game::process_flag::kBootDemo; }
    virtual void open_title_screen() = 0;        // black screen, title display mode, fade in
    virtual void demo_startup() = 0;             // one step of the attract demo boot
    virtual void reset_file_queue() = 0;
    virtual void play_sound(std::string_view name) = 0;
    virtual void fade_in() = 0;
    virtual void fade_out() = 0;
    virtual void fade_reset() = 0;
    virtual bool fade_finished() = 0;
    virtual void continue_game() = 0;            // the autosave or the newest save, then the map load
    virtual void load_save_screen() = 0;
    virtual void load_options_screen() = 0;
    virtual int difficulty_count() const = 0;
    virtual const char* difficulty_label(int index) const = 0;
    virtual int difficulty_default() const = 0;
    virtual void start_new_game(int difficulty) = 0;  // the new savegame and the map load
    virtual void load_movie_stream() = 0;
    virtual int file_queue_length() = 0;
    virtual void quit() = 0;
    virtual void finish_loading() = 0;           // display refresh and the file queue drained
    virtual void place_new_game_player() = 0;
    virtual void change_game_state(std::string_view name) = 0;
    virtual void clear_background() = 0;

    // Drawing (the game's text cursor: position, colour, then strings).
    virtual void draw_background() = 0;
    virtual void show_cursor() = 0;
    // The screen is played by touch (a phone or tablet): the rows take the layout's touch_row_pitch.
    virtual bool touch_screen() const { return false; }
    virtual void leave_title_screen() = 0;
    virtual bool text_translated(const char* text) const = 0;
    virtual int text_width(const char* text) const = 0;  // width of the translated text
    virtual void text_position(int x, int y) = 0;
    virtual void text_white() = 0;
    virtual void text(const char* text) = 0;
};

class TitleMenu {
public:
    enum class State : std::uint8_t { Start, Main, LoadGame, Difficulty, NewGameStart };

    void set_data(TitleMenuData data);
    const TitleMenuData& data() const { return data_; }

    // One frame of the title game state.
    void frame(TitleHost& host);

    // The continue entry is selected (a game over returns to the title with it).
    void select_continue();
    // Picks `kind` on the next main-menu frame as if it were chosen (the console's `options`), so its loads, fade
    // and the title's own leaving run as they do for the player. False unless the menu is up and has the entry.
    bool request_pick(TitleEntryKind kind);

    State state() const { return state_; }
    int selected() const { return selected_; }
    std::uint32_t visible() const { return visible_; }
    int difficulty() const { return difficulty_; }
    int previous_saves() const { return previous_saves_; }
    int skip_step() const { return skip_step_; }
    bool confirm_open() const { return confirm_open_; }
    bool confirm_yes() const { return confirm_yes_; }

    // Layout (exposed for tests): the y of the visible row `k` and the confirmation's question row.
    int row_y(int k) const;
    int confirm_y() const;

private:
    bool shown(int entry) const { return entry >= 0 && entry < 32 && ((visible_ >> entry) & 1u) != 0; }
    int count() const { return static_cast<int>(data_.entries.size()); }
    int visible_rows(int* order) const;
    void demo_tick(TitleHost& host);
    void main_frame(TitleHost& host, bool demo);
    void update_visible(TitleHost& host);
    void confirm_frame(TitleHost& host);
    int confirm_hover(const TitleHost& host, const TitleMouse& m) const;
    void mouse_rows(TitleHost& host);
    void pick(TitleHost& host);
    void difficulty_frame(TitleHost& host, bool demo);
    void hand_off(TitleHost& host);
    void idle_timer(TitleHost& host, bool demo);
    void draw(TitleHost& host);
    void draw_rows(TitleHost& host) const;
    void draw_confirm(TitleHost& host) const;
    void draw_difficulty(TitleHost& host) const;
    int half_width(const TitleHost& host, const char* label) const;
    int pitch() const;
    int lead() const;
    int difficulty_top(const TitleHost& host) const;

    TitleMenuData data_;
    bool touch_ = false;  // TitleHost::touch_screen of the frame
    State state_ = State::Start;
    int selected_ = 2;
    std::uint32_t visible_ = 0;
    int difficulty_ = 1;
    bool difficulty_from_data_ = false;
    int previous_saves_ = 0;
    int skip_step_ = 0;
    bool confirm_open_ = false;
    bool confirm_yes_ = false;
    bool quit_ = false;  // the host was asked to close the game
    int pick_request_ = -1;  // request_pick: the entry to pick on the next main frame
};

// Attract demos and intro movies shown since the title last had input (the running game's counter).
std::int8_t& title_demo_count();

}  // namespace sh::gameflow
