#pragma once

// The boot state and the table of screen handlers. The boot state runs
// four steps before the logos: set up the screen, hand the base sound banks to the sound driver one task
// per tick, queue the font and logo graphics (and, when the intros are skipped, everything the logos would
// have loaded), then wait for the fade-in and move on. The table maps the game state number to a handler
// by name (gamedata/config/boot.json); the host registers the handlers it has.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "engine/gameflow/map_load.h"
#include "formats/common/status.h"

namespace sh::gameflow {

struct BootParams {
    std::vector<std::int32_t> base_audio_tasks;  // sound driver tasks started one per tick, in order
    std::vector<std::string> state_handlers;     // handler name per game state number

    static BootParams builtin();
    static Result<BootParams> parse(std::string_view json);
};

class BootHost {
public:
    virtual ~BootHost() = default;

    // The session's memory and states unless a test host brings its own.
    virtual LoadView view() { return session_load_view(); }
    virtual int game_state() const { return sh::game::session().game.game_state; }
    virtual void game_state_set(int state) { sh::game::session().game.game_state = state; }
    virtual void game_state_previous_set(int state) { sh::game::session().game.game_state_previous = state; }
    virtual bool skip_intros() const = 0;

    virtual void background_black() { sh::game::session().game.clear_color.r = sh::game::session().game.clear_color.g = sh::game::session().game.clear_color.b = 0; }
    virtual void screen_init() = 0;
    virtual bool audio_streaming() const = 0;
    virtual void audio_call(int task) = 0;
    virtual void graphics_queue() = 0;      // font atlases first, then the menu font, then the logo
    virtual void intro_loads_run() = 0;     // what the logos load, when they are skipped
    virtual void fade_in_start() = 0;
    virtual bool fade_finished() const = 0;
    virtual void queue_wait_empty() = 0;
    virtual void play_state_gameplay_next() { sh::game::session().set_sys_state_next(sh::game::sys_state::kGameplay); }
    virtual void settings_restore_defaults() = 0;
    virtual void pad_hook(int arg) = 0;     // runs at the end of every boot tick
};

class BootMachine {
public:
    BootMachine(BootHost& host, BootParams params) : host_(host), params_(std::move(params)) {}

    void update();
    const BootParams& params() const { return params_; }

private:
    BootHost& host_;
    BootParams params_;
};

// Handler name for a game state number, or an empty view when the number is out of the table.
std::string_view state_handler_name(const BootParams& params, int game_state);

}  // namespace sh::gameflow
