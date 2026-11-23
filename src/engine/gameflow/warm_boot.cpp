#include "engine/gameflow/warm_boot.h"

namespace sh::gameflow {

namespace {
constexpr int kVibrationReset = 4;  // controller effect played over the reset
constexpr int kAudioStart = 19;     // sound driver commands: stop-all pair
constexpr int kAudioStop = 20;
constexpr int kAmbientOff = 1;
constexpr int kDemoNext = 1;
}  // namespace

void WarmBoot::wait_audio_idle() {
    while (host_.audio_streaming()) {
        host_.audio_tasks_run();
        host_.vsync_wait();
    }
}

void WarmBoot::run() {
    host_.presented_frame_release();
    host_.pause_menu_reset();

    host_.draw_sync();
    host_.screen_clear();
    host_.vibration_effect(kVibrationReset);
    host_.vibration_pump();
    host_.audio_call(kAudioStart);
    wait_audio_idle();
    host_.audio_call(kAudioStop);
    wait_audio_idle();

    host_.queue_reset();
    host_.queue_wait_empty();
    host_.audio_work_init();
    host_.ambient_set(kAmbientOff);
    wait_audio_idle();

    if (host_.demo_active()) {
        host_.demo_stop();
    }

    host_.play_state_clear();
    host_.demo_sequence_advance(kDemoNext);
    host_.demo_data_read();
    host_.title_graphics_load();
    host_.queue_wait_empty();
    host_.pad_update();

    const int previous = host_.game_state();
    host_.game_state_main_menu_set();

    const LoadView v = host_.view();
    v.counters[0] = 0;
    v.counters[1] = 0;
    v.steps[1] = 0;
    v.steps[2] = 0;

    host_.play_state_gameplay_next();
    host_.fade_out_start();

    host_.game_state_previous_set(previous);
    v.steps[0] = 0;
    host_.fade_timestep_zero();
}

}  // namespace sh::gameflow
