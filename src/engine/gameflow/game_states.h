#pragma once

// The in-game state machine. Every in-game frame one state
// runs: gameplay, a menu being opened, a room or map load, a message, a save, a map event script,
// the game over sequence or the pause. The engine owns the order of the decisions (which state
// runs, what the frame clock is during it, which state follows); the host owns the game objects
// those decisions touch (flags, sounds, scripts, menus, the player).
//
// Contract: with the same inputs the decisions equal the original's, including its order of side
// effects.

#include <cstdint>

#include "engine/game_state/session.h"
#include "engine/gameflow/event_triggers.h"

namespace sh::gameflow {

// Room and map loads (the trigger scan's door states).
constexpr bool is_area_load(GameState s) { return s == GameState::LoadRoom || s == GameState::LoadMap; }

// ---------------------------------------------------------------------------------------------
// Gameplay state: what follows a gameplay frame.

enum class MenuFlag : std::uint8_t { Keep, Set, Clear };

struct GameplayInput {
    GameState state = GameState::Gameplay;  // the state when the decision is made
    GameState map_event = GameState::None;  // the state the trigger scan picked this frame
    bool alive = true;
    bool attacking = false;                 // an attack is in progress: menus wait for it
    bool light_pressed = false;
    bool light_available = false;           // the scene allows the flashlight to be switched
    bool pause_pressed = false;
    bool status_pressed = false;            // inventory
    bool map_pressed = false;
    bool options_pressed = false;
    bool fade_idle = false;                 // no screen fade is running
};

struct GameplayDecision {
    bool toggle_light = false;
    GameState next = GameState::None;       // None = stay in the current state
    bool clear_message = false;             // opening the map drops a pending message string
    MenuFlag menu = MenuFlag::Keep;         // the "menu is open" flag of the game
};

// A map event wins over every button; pause beats the menus; while an attack is in progress no menu
// opens and the menu flag is left alone. Dead players decide nothing.
GameplayDecision decide_gameplay(const GameplayInput& in);

// ---------------------------------------------------------------------------------------------
// The state machine and its host.

// What a message box answered this frame.
enum class MessageResult : std::int8_t { Finished = -1, Showing = 0, Choice0 = 1, Choice1 = 2, Choice2 = 3 };

// The event the trigger scan picked last (the one a state such as ReadMessage belongs to).
struct RunningEvent {
    bool present = false;
    bool button = false;           // activated with the action button
    std::int16_t done_flag = 0;    // the flag set when the event has run (its blocking flag)
    std::uint8_t options = 0;      // EventOption bits
    int param = 0;                 // state parameter (message, sound, script, ...)
};

// Option bits of a map event.
enum EventOption : std::uint8_t {
    kEventKeepsWorldRunning = 1 << 0,  // a message of this event does not stop the world
};

struct StateRules {
    std::int32_t sound_base = 0;       // sound number of an EventPlaySound parameter 0
    int state_count = 15;              // states the machine knows; others fall back to gameplay
};

class StateHost {
public:
    virtual ~StateHost() = default;

    // The session's in-game state unless a test host brings its own.
    virtual GameState state() const { return static_cast<GameState>(static_cast<std::uint8_t>(sh::game::session().sys_state())); }
    // Starts a state: its steps and timers start from zero.
    virtual void enter_state(GameState s) { sh::game::session().set_sys_state_next(static_cast<std::int32_t>(s)); }
    // Changes the state only (a plain store, which keeps the steps).
    virtual void store_state(GameState s) { sh::game::session().system.flow.sys_state = static_cast<std::int32_t>(s); }

    virtual bool player_alive() const = 0;

    // The frame clock (Q12 seconds): the delta the world runs on and the unscaled one.
    virtual std::int32_t delta() const = 0;
    virtual std::int32_t raw_delta() const = 0;
    virtual void set_delta(std::int32_t d) = 0;

    virtual bool message_set() const { return sh::game::session().system.flow.message_shown != 0; }
    virtual void clear_message_set() { sh::game::session().system.flow.message_shown = 0; }
    // A script's message shows over play (sh.world.say): gameplay keeps its message box set up.
    virtual bool soft_message_active() const { return false; }

    virtual void run_gameplay() = 0;
    // A state the game still runs itself (the room and map load and the game over have their own modules).
    virtual void run_state(GameState s) = 0;
    // Runs a menu state (pause, options, items, map, movie, save: menu_states.h); false = the game runs it itself.
    virtual bool run_menu_state(GameState s) { (void)s; return false; }

    virtual RunningEvent running_event() const = 0;
    // The trigger scan with button events off; returns the state it picked (None = none).
    virtual GameState scan_events() = 0;
    // Called before a state picked by the scan starts (the host's own presentation work).
    virtual void before_event_state(GameState s) { (void)s; }

    virtual void set_flag(std::int16_t flag) = 0;
    virtual void play_sound(std::int32_t sound) = 0;
    // Runs one tick of the map's event script `param`; false when the map has none.
    virtual bool run_event_script(int param) = 0;
    // Mod hook (map.event), asked every tick of an event callback before anything of it runs: false skips the
    // handler and the event ends without its done flag.
    virtual bool event_script_allowed(int param) { (void)param; return true; }

    // ReadMessage.
    // A scene lets the world run under messages.
    virtual bool scene_keeps_world_running() const { return sh::game::session().sys_flag(sh::game::sys_flag::kSceneLock); }
    virtual bool enemy_alive() const = 0;
    virtual void freeze_player() = 0;
    virtual void unfreeze_player() = 0;
    virtual MessageResult show_message(int message) = 0;

    // The event screen game state (map events that own the whole screen).
    virtual int event_screen_step() const { return sh::game::session().game.game_steps[0]; }
    virtual void start_event_screen() = 0;   // first frame: frame pacing and the fade in
    virtual void update_ready() = 0;         // the "fade done and files loaded" signal
    virtual void draw_event_screen() = 0;
};

class StateMachine {
public:
    void set_rules(const StateRules& r) { rules_ = r; }

    // One in-game frame: picks and runs the state, and after a non-gameplay state that returned to
    // gameplay, runs the trigger scan so an event can start on the same frame.
    void frame(StateHost& host);

    // The event screen game state (one frame).
    void event_screen_frame(StateHost& host);

    // The delta of the last frame that had time running (the states that resume the world use it).
    std::int32_t saved_delta() const { return saved_delta_; }

    // The states run by the engine (exposed for tests).
    void event_set_flag(StateHost& host);
    void event_play_sound(StateHost& host);
    void event_callback(StateHost& host);
    void read_message(StateHost& host);

private:
    void run(GameState s, StateHost& host);

    StateRules rules_{};
    std::int32_t saved_delta_ = 0;
};

}  // namespace sh::gameflow
