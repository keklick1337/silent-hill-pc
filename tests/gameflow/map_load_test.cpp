// The map load machine on a recording host: step order per load kind, the demo retry counter, the chunk
// and music watchdogs, the loading-screen pace clock and the data file.

#include <string>
#include <vector>

#include "engine/gameflow/map_load.h"

#include "framework/sh_test.h"

namespace gf = sh::gameflow;

namespace {

class Host final : public gf::MapLoadHost {
public:
    std::int32_t steps[3] = {0, 0, 0};
    std::int32_t counters[3] = {0, 0, 0};
    gf::LoadKind load_kind = gf::LoadKind::RoomTransition;
    bool demo = false, player_run = false, chunks = true, demo_data = true, flag = false;
    int queue = 0, vsync = 0, music = 0, ambient = 0, events = 0, flag_count = 0, dt = 33;
    std::vector<std::string> log;

    gf::LoadView view() override { return {steps, counters}; }
    gf::LoadKind kind() const override { return load_kind; }
    bool demo_active() const override { return demo; }
    void demo_active_set() override { demo = true; log.push_back("demo_active_set"); }
    bool event_flag(int i) const override { return (events >> i) & 1; }
    int queue_length() const override { return queue; }
    void queue_update() override { log.push_back("queue_update"); --queue; }
    void queue_drain_now() override { log.push_back("drain"); }
    bool audio_streaming() const override { return false; }
    int vsync_count() override { return vsync; }
    std::int32_t delta_time() const override { return dt; }
    void hold_feedback_frame() override { log.push_back("hold_feedback"); }
#define STEP(name) void name() override { log.push_back(#name); }
    STEP(background_clear) STEP(area_player_position_update) STEP(demo_savegame_update) STEP(player_init)
    STEP(demo_map_load) STEP(demo_sequence_advance) STEP(demo_data_read) STEP(demo_play_data_read) STEP(demo_start)
    STEP(loading_screen_player_run_set) STEP(background_motion_blur) STEP(chara_groups_load) STEP(chara_groups_loaded)
    STEP(world_clear) STEP(player_chunk_init) STEP(radio_stop) STEP(audio_state_force_clear) STEP(npc_init)
    STEP(in_game_init) STEP(transition_sound) STEP(state_main_load_screen_set) STEP(state_in_game_next)
    STEP(fade_reset_timestep) STEP(load_flag_clear) STEP(load_done_sounds)
#undef STEP
    void interval_vblanks_set(int v) override { log.push_back("interval " + std::to_string(v)); }
    void audio_call(int s) override { log.push_back("audio " + std::to_string(s)); }
    bool demo_buffer_setup() override { return demo_data; }
    bool loading_screen_player_run() const override { return player_run; }
    bool loading_screen_shown() const override { return player_run; }
    void loading_screen_draw(int t) override { log.push_back("screen " + std::to_string(t)); }
    void chara_group_alloc(int s) override { log.push_back("alloc " + std::to_string(s)); }
    bool chunks_ready() override { return chunks; }
    int music_init() override { return music; }
    int ambient_init() override { return ambient; }
    bool load_flag() const override { return flag; }
    int load_flag_tick() override { return ++flag_count; }
};

bool has(const Host& h, const std::string& s) {
    for (const std::string& l : h.log) {
        if (l == s) return true;
    }
    return false;
}

void run(gf::MapLoad& m, int frames) {
    for (int i = 0; i < frames; ++i) m.startup();
}

}  // namespace

SH_TEST(map_load_room_transition_path) {
    Host h;
    gf::MapLoad m(h);
    m.startup();  // step 0 -> 7
    SH_CHECK(h.steps[0] == 7);
    SH_CHECK((h.log == std::vector<std::string>{"area_player_position_update", "audio 19"}) ||
             (h.log.front() == "interval 1" && has(h, "area_player_position_update") && has(h, "audio 19")));
    h.log.clear();
    m.startup();  // 7 falls into 8: chunks ready -> 9
    SH_CHECK(h.steps[0] == 9 && h.log == std::vector<std::string>{"player_chunk_init"});
    h.log.clear();
    m.startup();  // music started -> 10
    SH_CHECK(h.steps[0] == 10 && has(h, "state_main_load_screen_set"));
    m.startup();  // ambient ready -> 11
    SH_CHECK(h.steps[0] == 11);
    h.log.clear();
    m.startup();  // door loads do not wait
    SH_CHECK(h.steps[0] == 12 && has(h, "npc_init") && has(h, "transition_sound") && !has(h, "in_game_init"));
    m.startup();
    SH_CHECK(has(h, "state_in_game_next") && h.steps[0] == 12);
}

SH_TEST(map_load_overlay_and_continue_paths) {
    Host h;
    h.load_kind = gf::LoadKind::Continue;
    gf::MapLoad m(h);
    m.startup();
    SH_CHECK(h.steps[0] == 3);
    m.startup();
    SH_CHECK(h.steps[0] == 4);
    m.startup();
    SH_CHECK(has(h, "loading_screen_player_run_set") && h.steps[0] == 5);
    h.log.clear();
    h.queue = 2;
    m.startup();  // 5 falls into 6 and waits for the queue
    SH_CHECK(h.steps[0] == 6 && h.log[0] == "alloc 1" && h.log[1] == "alloc 2" && h.log[2] == "alloc 3");
    SH_CHECK(h.log[3] == "chara_groups_load" && !has(h, "chara_groups_loaded"));
    h.queue = 0;
    m.startup();
    SH_CHECK(h.steps[0] == 7 && has(h, "chara_groups_loaded"));
    m.startup();
    SH_CHECK(h.steps[0] == 9 && !has(h, "radio_stop"));
    h.steps[0] = 11;
    h.counters[0] = 29;
    m.startup();
    SH_CHECK(h.steps[0] == 11);  // a non-door load holds 30 frames
    h.counters[0] = 30;
    m.startup();
    SH_CHECK(h.steps[0] == 12 && has(h, "in_game_init") && !has(h, "transition_sound"));
    h.events = 2;
    m.startup();
    SH_CHECK(h.steps[0] == 1 && has(h, "fade_reset_timestep"));
}

SH_TEST(map_load_overlay_radio_and_player_position) {
    Host h;
    h.load_kind = gf::LoadKind::OverlayTransition;
    h.events = 1;
    gf::MapLoad m(h);
    m.startup();
    SH_CHECK(h.steps[0] == 3);
    h.steps[0] = 4;
    m.startup();
    SH_CHECK(has(h, "area_player_position_update"));
    h.steps[0] = 7;
    m.startup();
    SH_CHECK(has(h, "world_clear") && has(h, "radio_stop"));
}

SH_TEST(map_load_boot_demo_retries) {
    Host h;
    h.load_kind = gf::LoadKind::BootDemo;
    h.demo_data = false;
    gf::MapLoad m(h);
    m.startup();
    SH_CHECK(h.steps[0] == 1 && h.counters[1] == 1);
    m.startup();  // the title wait is not over
    SH_CHECK(!has(h, "player_init"));
    h.counters[1] = 1201;
    for (int i = 0; i < 4; ++i) {
        m.startup();
        SH_CHECK(h.counters[1] == 1201 && h.steps[0] == 1);
    }
    m.startup();  // fifth failure starts the wait over
    SH_CHECK(h.counters[1] == 0);
    int advances = 0;
    for (const std::string& l : h.log) advances += l == "demo_sequence_advance";
    SH_CHECK(advances == 5);
    h.demo_data = true;
    h.counters[1] = 1201;
    m.startup();
    SH_CHECK(h.steps[0] == 2 && has(h, "demo_map_load") && h.counters[1] == 0);
    m.startup();
    SH_CHECK(h.steps[0] == 3 && has(h, "demo_play_data_read"));
    h.steps[0] = 10;
    m.startup();
    SH_CHECK(has(h, "demo_start") && h.demo);
}

SH_TEST(map_load_chunk_and_music_watchdogs) {
    Host h;
    h.chunks = false;
    h.queue = 3;
    gf::MapLoad m(h);
    h.steps[0] = 8;
    m.startup();
    SH_CHECK(h.queue == 0 && h.steps[0] == 8);  // drained, chunks not ready
    for (int i = 0; i < 119; ++i) m.startup();
    SH_CHECK(h.steps[0] == 8);
    m.startup();  // the 121st frame gives up waiting
    SH_CHECK(h.steps[0] == 9);
    h.music = 1;
    h.steps[1] = 7;
    h.log.clear();
    run(m, 300);
    SH_CHECK(!has(h, "audio_state_force_clear") && h.steps[0] == 9);
    m.startup();
    SH_CHECK(has(h, "audio_state_force_clear") && h.steps[1] == 0);
}

SH_TEST(map_load_player_run_screen_clock) {
    Host h;
    h.player_run = true;
    h.load_kind = gf::LoadKind::Continue;
    gf::MapLoad m(h);
    h.steps[0] = 9;
    h.vsync = 1000;
    m.loading_screen();  // first step at once
    SH_CHECK(m.screen_paced() && m.screen_step() == 1 && m.screen_dt() == 4096 * 2 / 60);
    SH_CHECK(has(h, "screen 3276") && has(h, "background_motion_blur"));
    h.vsync = 1001;
    m.loading_screen();
    SH_CHECK(m.screen_step() == 1 && m.screen_dt() == 0 && has(h, "hold_feedback"));
    h.vsync = 1002;
    m.loading_screen();
    SH_CHECK(m.screen_step() == 2);
    // The music start waits out the minimum time.
    h.log.clear();
    m.startup();
    SH_CHECK(h.steps[0] == 9 && !has(h, "state_main_load_screen_set"));
    h.vsync = 1000 + 180;
    m.startup();
    SH_CHECK(h.steps[0] == 10);
    // Past step 10 the screen is not drawn and the frame time passes through.
    m.loading_screen();
    SH_CHECK(m.screen_dt() == 33 && !m.screen_paced());
}

SH_TEST(map_load_load_flag_sounds) {
    Host h;
    h.flag = true;
    h.steps[0] = 99;
    gf::MapLoad m(h);
    for (int i = 0; i < 20; ++i) m.screen_update();
    SH_CHECK(!has(h, "load_flag_clear"));
    m.screen_update();
    SH_CHECK(has(h, "load_flag_clear") && has(h, "load_done_sounds"));
}

SH_TEST(map_load_params_file) {
    auto ok = gf::MapLoadParams::parse("{\"demo_attempts\": 3, \"hold_frames\": 60}");
    SH_CHECK(ok && ok.value().demo_attempts == 3 && ok.value().hold_frames == 60 && ok.value().min_vblanks == 180);
    SH_CHECK(!gf::MapLoadParams::parse("{\"vblank_rate\": 0}"));
    SH_CHECK(!gf::MapLoadParams::parse("{\"hold_frames\": \"x\"}"));
    SH_CHECK(!gf::MapLoadParams::parse("["));
}
