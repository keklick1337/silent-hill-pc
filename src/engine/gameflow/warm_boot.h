#pragma once

// The warm reset: what the game does when it returns to the title
// from play - drop the frozen frame, silence sound and controller effects, empty the file queue, forget
// the play state and bring the title graphics back. The reset only orders the steps and the waits; the
// systems are reached through WarmBootHost. The same step and counter memory as the load machine
// (map_load.h) is cleared through its view.

#include "engine/gameflow/map_load.h"

namespace sh::gameflow {

class WarmBootHost {
public:
    virtual ~WarmBootHost() = default;

    // The session's memory and states unless a test host brings its own.
    virtual LoadView view() { return session_load_view(); }
    virtual int game_state() const { return sh::game::session().game.game_state; }
    virtual void game_state_main_menu_set() { sh::game::session().game.game_state = sh::game::game_state::kMainMenu; }
    virtual void game_state_previous_set(int state) { sh::game::session().game.game_state_previous = state; }

    virtual void presented_frame_release() = 0;  // the freeze frame of pause, map messages and pickups
    virtual void pause_menu_reset() = 0;
    virtual void draw_sync() = 0;
    virtual void screen_clear() = 0;
    virtual void vibration_effect(int effect) = 0;
    virtual void vibration_pump() = 0;
    virtual void audio_call(int command) = 0;
    virtual bool audio_streaming() const = 0;
    virtual void audio_tasks_run() = 0;
    virtual void vsync_wait() = 0;
    virtual void queue_reset() = 0;
    virtual void queue_wait_empty() = 0;
    virtual void audio_work_init() = 0;
    virtual void ambient_set(int mode) = 0;
    virtual bool demo_active() const { return sh::game::session().sys_flag(sh::game::sys_flag::kDemo); }
    virtual void demo_stop() = 0;
    virtual void play_state_clear() { sh::game::session().clear_system(); }
    virtual void demo_sequence_advance(int step) = 0;
    virtual void demo_data_read() = 0;
    virtual void title_graphics_load() = 0;
    virtual void pad_update() = 0;
    virtual void play_state_gameplay_next() { sh::game::session().set_sys_state_next(sh::game::sys_state::kGameplay); }
    virtual void fade_out_start() = 0;
    virtual void fade_timestep_zero() = 0;
};

class WarmBoot {
public:
    explicit WarmBoot(WarmBootHost& host) : host_(host) {}

    void run();

private:
    void wait_audio_idle();

    WarmBootHost& host_;
};

}  // namespace sh::gameflow
