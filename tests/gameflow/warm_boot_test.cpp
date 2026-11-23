// The warm reset order on a recording host: stop-all audio waits poll until the driver is idle, the play
// state is cleared before the title graphics load, and the counters and steps end up zeroed.

#include <string>
#include <vector>

#include "engine/gameflow/warm_boot.h"

#include "framework/sh_test.h"

namespace gf = sh::gameflow;

namespace {

class Host final : public gf::WarmBootHost {
public:
    std::int32_t steps[3] = {5, 6, 7};
    std::int32_t counters[3] = {8, 9, 10};
    int state = 4, prev = -1, streaming_polls = 0;
    bool demo = false;
    std::vector<std::string> log;

    gf::LoadView view() override { return {steps, counters}; }
    int game_state() const override { return state; }
    void game_state_main_menu_set() override { state = 100; log.push_back("main_menu"); }
    void game_state_previous_set(int s) override { prev = s; }
    bool audio_streaming() const override {
        auto* self = const_cast<Host*>(this);
        return self->streaming_polls > 0 && self->streaming_polls-- > 0;
    }
    bool demo_active() const override { return demo; }
    void audio_call(int c) override { log.push_back("audio " + std::to_string(c)); }
    void ambient_set(int m) override { log.push_back("ambient " + std::to_string(m)); }
    void vibration_effect(int e) override { log.push_back("vib " + std::to_string(e)); }
    void demo_sequence_advance(int s) override { log.push_back("demo_seq " + std::to_string(s)); }
#define STEP(name) void name() override { log.push_back(#name); }
    STEP(presented_frame_release) STEP(pause_menu_reset) STEP(draw_sync) STEP(screen_clear) STEP(vibration_pump)
    STEP(audio_tasks_run) STEP(vsync_wait) STEP(queue_reset) STEP(queue_wait_empty) STEP(audio_work_init) STEP(demo_stop)
    STEP(play_state_clear) STEP(demo_data_read) STEP(title_graphics_load) STEP(pad_update) STEP(play_state_gameplay_next)
    STEP(fade_out_start) STEP(fade_timestep_zero)
#undef STEP
};

int count(const Host& h, const std::string& s) {
    int n = 0;
    for (const std::string& l : h.log) n += l == s;
    return n;
}

}  // namespace

SH_TEST(warm_boot_order_and_final_state) {
    Host h;
    gf::WarmBoot(h).run();
    SH_CHECK(h.log.front() == "presented_frame_release" && h.log[1] == "pause_menu_reset");
    SH_CHECK(h.log[2] == "draw_sync" && h.log[3] == "screen_clear" && h.log[4] == "vib 4" && h.log[5] == "vibration_pump");
    SH_CHECK(h.log[6] == "audio 19" && h.log[7] == "audio 20");
    SH_CHECK(count(h, "demo_stop") == 0 && count(h, "queue_wait_empty") == 2 && count(h, "ambient 1") == 1);
    SH_CHECK(h.log.back() == "fade_timestep_zero");
    SH_CHECK(h.steps[0] == 0 && h.steps[1] == 0 && h.steps[2] == 0 && h.counters[0] == 0 && h.counters[1] == 0 &&
             h.counters[2] == 10);
    SH_CHECK(h.prev == 4 && h.state == 100);
}

SH_TEST(warm_boot_waits_for_audio_and_stops_demo) {
    Host h;
    h.streaming_polls = 3;
    h.demo = true;
    gf::WarmBoot(h).run();
    // The first wait runs the task pool three times, one vsync each.
    SH_CHECK(count(h, "audio_tasks_run") == 3 && count(h, "vsync_wait") == 3 && count(h, "demo_stop") == 1);
}
