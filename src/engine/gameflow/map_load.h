#pragma once

// The map load machine: the steps that run while a loading
// screen is up - waiting for the file queue, loading the map's character groups and chunks,
// starting the area's music, then handing over to play - and the clock that paces the loading
// screen. The machine owns the order, the conditions between steps and the retry counters; the
// systems the steps drive (files, audio, world, screens) are reached through MapLoadHost. The step
// numbers and counters live in game-owned memory, so the machine works on a view over them.

#include <cstdint>
#include <string_view>

#include "formats/common/status.h"

#include "engine/game_state/session.h"

namespace sh::gameflow {

// Numbers of the machine (gamedata/config/map_load.json). The three pace values are changed at run
// time by the console and the configuration file.
struct MapLoadParams {
    std::int32_t demo_wait_frames = 1200;      // idle frames on the title before a demo is loaded
    std::int32_t demo_attempts = 5;            // demo load tries before the wait starts over
    std::int32_t queue_flush_limit = 500;      // queue updates spent draining pending reads per frame
    std::int32_t chunk_wait_frames = 120;      // frames to wait for map chunks before going on without them
    std::int32_t music_watchdog_frames = 300;  // frames of failing music start before the audio state is reset
    std::int32_t load_sound_frames = 21;       // frames the load flag stays up before the two load sounds
    std::int32_t last_screen_step = 10;        // the loading screen is drawn while the step is below this
    std::int32_t screen_fade_timestep = 3276;  // Q12 fade speed of the loading screen
    std::int32_t pace_step_limit = 2;          // longest screen step, in vblanks, Harry may advance
    std::int32_t vblank_rate = 60;             // vblanks per second the step length is scaled by
    // Run time tunables.
    std::int32_t hold_frames = 30;             // minimum frames of a non-door load screen
    std::int32_t pace_vblanks = 2;             // vblanks per step of the Harry-running screen (0 or 1 = every frame)
    std::int32_t min_vblanks = 180;            // minimum time of the Harry-running screen

    static Result<MapLoadParams> parse(std::string_view json);
};

enum class LoadKind {
    None,
    RoomTransition,
    OverlayTransition,
    NewGame,
    LoadSave,
    Continue,
    BootDemo,
    Other,  // any combination of flags
};

// The game's step and counter memory. steps[0] is the machine's step; steps[1] and steps[2] belong to
// the sub-steps of the systems it calls. counters[0] counts frames since the transition began,
// counters[1] frames in the current step.
struct LoadView {
    std::int32_t* steps = nullptr;     // 3 entries
    std::int32_t* counters = nullptr;  // 3 entries
};

// The running game's step and counter memory: the screen's steps and the frame counters.
inline LoadView session_load_view()
{
    return {sh::game::session().game.game_steps, sh::game::session().system.flow.frame_counters};
}

// The kind of load the session's process flags ask for.
LoadKind session_load_kind();

// The loading screen of a player running in the dark (the map's loading screen 1).
constexpr std::int8_t kLoadingScreenPlayerRun = 1;

class MapLoadHost {
public:
    virtual ~MapLoadHost() = default;

    // The session's memory and flags unless a test host brings its own.
    virtual LoadView view() { return session_load_view(); }
    virtual LoadKind kind() const { return session_load_kind(); }
    virtual bool demo_active() const { return sh::game::session().sys_flag(sh::game::sys_flag::kDemo); }
    virtual void demo_active_set() { sh::game::session().set_sys_flag(sh::game::sys_flag::kDemo, true); }
    virtual bool event_flag(int index) const = 0;  // the map event's three flags: world frozen, to-play, no ambient wait

    virtual int queue_length() const = 0;
    virtual void queue_update() = 0;
    virtual void queue_drain_now() = 0;  // run the queue at storage speed (door loads only)
    virtual bool audio_streaming() const = 0;
    virtual int vsync_count() = 0;
    virtual std::int32_t delta_time() const = 0;  // Q12 seconds of the frame
    virtual void hold_feedback_frame() = 0;  // the screen keeps this frame's feedback buffer

    // Step 0.
    virtual void interval_vblanks_set(int vblanks) = 0;
    virtual void background_clear() { sh::game::session().game.clear_color.r = sh::game::session().game.clear_color.g = sh::game::session().game.clear_color.b = 0; }
    virtual void area_player_position_update() = 0;
    virtual void audio_call(int sound) = 0;  // fire-and-forget sound driver call
    // Demo steps.
    virtual void demo_savegame_update() = 0;
    virtual void player_init() = 0;
    virtual bool demo_buffer_setup() = 0;  // false = no demo data to play
    virtual void demo_map_load() = 0;
    virtual void demo_sequence_advance() = 0;
    virtual void demo_data_read() = 0;
    virtual void demo_play_data_read() = 0;
    virtual void demo_start() = 0;
    // Steps 4 to 12.
    virtual void loading_screen_player_run_set() { sh::game::session().system.scene.loading_screen = kLoadingScreenPlayerRun; }
    virtual bool loading_screen_player_run() const { return sh::game::session().system.scene.loading_screen == kLoadingScreenPlayerRun; }
    virtual bool loading_screen_shown() const { return sh::game::session().system.scene.loading_screen != 0; }
    virtual void loading_screen_draw(int fade_timestep) = 0;
    virtual void background_motion_blur() = 0;
    virtual void chara_group_alloc(int slot) = 0;  // slots 1..3: the map's character groups
    virtual void chara_groups_load() = 0;
    virtual void chara_groups_loaded() = 0;  // the queue is idle after chara_groups_load
    virtual void world_clear() = 0;
    virtual void player_chunk_init() = 0;
    virtual void radio_stop() = 0;
    virtual bool chunks_ready() = 0;
    virtual int music_init() = 0;  // 0 = started
    virtual void audio_state_force_clear() = 0;
    virtual int ambient_init() = 0;  // 0 = ready
    virtual void npc_init() = 0;
    virtual void in_game_init() = 0;
    virtual void transition_sound() = 0;
    virtual void state_main_load_screen_set() { sh::game::session().game.game_state = sh::game::game_state::kMainLoadScreen; }
    virtual void state_in_game_next() { sh::game::session().set_game_state_next(sh::game::game_state::kInGame); }
    virtual void fade_reset_timestep() = 0;
    // The load flag of the loader screens.
    virtual bool load_flag() const { return sh::game::session().sys_flag(sh::game::sys_flag::kLoading); }
    virtual int load_flag_tick() = 0;  // counts a frame of the flag, returns the count
    virtual void load_flag_clear() { sh::game::session().set_sys_flag(sh::game::sys_flag::kLoading, false); }
    virtual void load_done_sounds() = 0;
};

class MapLoad {
public:
    explicit MapLoad(MapLoadHost& host, MapLoadParams params = {}) : host_(host), params_(params) {}

    MapLoadParams& params() { return params_; }

    // One frame of the load-screen game state (draw the screen, run one step, count the load flag).
    void screen_update();
    // One step of the machine.
    void startup();
    // Draws the loading screen with its own clock.
    void loading_screen();

    // The pace clock of the Harry-running screen.
    bool screen_paced() const;
    std::int32_t screen_step() const { return screen_step_; }
    // Q12 seconds the Harry-running screen advances by this frame.
    std::int32_t screen_dt() const { return screen_dt_; }

private:
    bool door_load() const;
    bool player_run_screen() const;
    void clock_tick();
    bool holding_minimum();
    void clock_reset();
    void step_next();

    MapLoadHost& host_;
    MapLoadParams params_;
    std::int32_t start_vblank_ = -1;
    std::int32_t step_vblank_ = -1;
    std::int32_t screen_step_ = 0;
    std::int32_t screen_dt_ = 0;
    std::int32_t demo_attempts_ = 0;
    std::int32_t chunk_frames_ = 0;
    std::int32_t music_frames_ = 0;
};

}  // namespace sh::gameflow
