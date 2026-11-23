// SPDX-License-Identifier: GPL-3.0-or-later
//
// The screens' view of the game (engine/screens/screens_view.h) that is not drawing: video memory kept aside and put
// back, the waits for the GPU and the vertical blank, and the sound, timer and title chores. The 2D placement and the
// background pictures are screens_host.cpp's.

#include <cstdint>

#include "engine/app/gameflow/game_flow.h"
#include "engine/entities/character_record.h"
#include "engine/screens/screens_view.h"
#include "engine/timing/vblank.h"
#include "render/gpu/game_display.h"
#include "render/gpu/renderer.h"

extern "C" {

void Sd_SetVolume(std::uint8_t voice, std::int16_t music, std::uint8_t effects);
void Sd_AllSfxWithRRStop(void);
void Bgm_TrackIdxUpdate(void);
void Game_TimerUpdate(void);
void Game_RadioSoundStop(void);
void Options_BrightnessMenu_LinesDraw(std::int32_t level);
// The first-person eye baseline in Harry's body frame (src/input/alt_camera_host.cpp).
extern sh::entities::Vec3 g_PcFpsOffset;
}

namespace sh::screens {

namespace {

// The console waits for the eighth vertical blank since the last wait.
constexpr int kWaitEightBlanks = 8;

sh::gpu::Rect rect(int x, int y, int w, int h) {
    return sh::gpu::Rect{static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), static_cast<std::int16_t>(w),
                         static_cast<std::int16_t>(h)};
}

}  // namespace

void keep_vram(int x, int y, int w, int h, std::uint16_t* pixels) {
    sh::gpu::display::sync();
    if (sh::gpu::Renderer* r = sh::gpu::renderer()) {
        r->store_image(rect(x, y, w, h), pixels);
    }
    sh::gpu::display::sync();
}

void restore_vram(int x, int y, int w, int h, const std::uint16_t* pixels) {
    if (sh::gpu::Renderer* r = sh::gpu::renderer()) {
        r->load_image(rect(x, y, w, h), pixels);
    }
    sh::gpu::display::sync();
}

void set_sound_volumes(int voice, int music, int effects) {
    Sd_SetVolume(static_cast<std::uint8_t>(voice), static_cast<std::int16_t>(music),
                 static_cast<std::uint8_t>(effects));
}
void update_music_track() { Bgm_TrackIdxUpdate(); }
void update_play_timer() { Game_TimerUpdate(); }
void stop_radio_sound() { Game_RadioSoundStop(); }
void wait_frame_done(bool vblanks) {
    sh::gpu::display::sync();
    if (vblanks) {
        sh::timing::wait_vblanks(kWaitEightBlanks);
    }
}
void draw_brightness_lines(int level) { Options_BrightnessMenu_LinesDraw(level); }
void start_title_loads() { sh::app::title_graphics_load(); }
void stop_all_sound_effects() { Sd_AllSfxWithRRStop(); }
void set_fps_head_axis(int axis, int value) {
    (axis == 0 ? g_PcFpsOffset.x : axis == 1 ? g_PcFpsOffset.y : g_PcFpsOffset.z) = value;
}

}  // namespace sh::screens
