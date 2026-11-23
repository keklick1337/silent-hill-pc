// SPDX-License-Identifier: GPL-3.0-or-later
//
// The hosts of the game's one screen fade and one pair of letterbox bars (engine/screens/screen_fade.h):
// the fade's status and custom step, the bars' state, the clock, the window's real size for the bars' reach, and
// their slots in the front 2D list. Over the session and the screens' views.

#include <cstdint>

#include "engine/app/loader/game_files.h"
#include "engine/config/pc_config.h"
#include "engine/game_state/game_state_ui.h"
#include "engine/game_state/session.h"
#include "engine/loop/frame_view.h"
#include "engine/screens/screens_view.h"
#include "render/gpu/render_state.h"

extern "C" {
// The custom fade step: the game flow writes it by this name, the fade reads it.
float GR_LivePixelAspect(void);
void vcSetEvCamRate(std::int16_t ev_cam_rate);
void vcChangeProjectionValue(std::int16_t scr_y);
}

namespace sh::screens {
namespace {

using sh::game::session;

constexpr int kFadeSlot = 5;
constexpr int kBorderSlot = 4;

// Packet identities of the 2D slots (nothing is built there; the frame anchors by order-table position).
struct Keys {
    unsigned char fade[2][16];
    unsigned char border[2][16];
} s_keys;

int buffer_index() { return sh::game_state::frame_buffer_index(); }

class FadeHost final : public ScreenFadeHost {
public:
    std::int32_t status() override { return fade_status(); }
    void set_status(std::int32_t status) override { set_fade_status(status); }
    std::int32_t timestep() override { return sh::screens::fade_control().timestep; }
    void set_timestep(std::int32_t step) override { sh::screens::fade_control().timestep = step; }
    int file_queue_length() override { return sh::loader::queue_length(); }
    std::int32_t frame_time() override { return sh::loop::frame_delta_raw(); }
    int buffer() override { return buffer_index(); }
    void submit(const draw2d::Command& command) override {
        link_2d(Table2d::Hud, kFadeSlot, s_keys.fade[buffer_index()], command);
    }
};

class BordersHost final : public CutsceneBordersHost {
public:
    std::int32_t state() override { return session().system.flow.cutscene_border; }
    void set_state(std::int32_t state) override { session().system.flow.cutscene_border = state; }
    bool music_paused() override { return session().music_status(sh::game::music_status::kPause); }
    std::int32_t frame_time() override { return sh::loop::frame_delta(); }
    int buffer() override { return buffer_index(); }
    bool cutscene_active() override { return session().sys_flag(sh::game::sys_flag::kCutscene); }
    void clear_cutscene_active() override { session().set_sys_flag(sh::game::sys_flag::kCutscene, false); }
    int screen_height() override { return session().game.screen_height; }
    int half_width() override {
        // The real drawable size (a borderless window on a wide display is wider than the configured one), so the
        // bars reach the window edges at any aspect.
        const int rw =
            sh::render::published().scene_width > 0 ? sh::render::published().scene_width : g_PcConfig.windowWidth;
        const int rh =
            sh::render::published().scene_height > 0 ? sh::render::published().scene_height : g_PcConfig.windowHeight;
        const float hs = (sh::render::picture().world_h_scale > 0.01f) ? sh::render::picture().world_h_scale : 1.0f;
        float fw = (rh > 0) ? ((float)session().game.screen_height * (float)rw / (2.0f * (float)rh)) *
                                  GR_LivePixelAspect() / hs
                            : 160.0f;
        if (fw < 160.0f) fw = 160.0f;
        return static_cast<std::int16_t>(fw + 10.0f);
    }
    int bar_outer() override { return static_cast<std::int16_t>(sh::render::picture().letterbox_outer); }
    int bar_inner() override { return static_cast<std::int16_t>(sh::render::picture().letterbox_inner); }
    void set_event_camera_rate(std::int32_t rate) override { vcSetEvCamRate(static_cast<std::int16_t>(rate)); }
    void set_projection(int screen_distance) override {
        vcChangeProjectionValue(static_cast<std::int16_t>(screen_distance));
    }
    void submit(const draw2d::Command& command) override {
        link_2d(Table2d::Hud, kBorderSlot, s_keys.border[buffer_index()], command);
    }
};

}  // namespace

void run_screen_fade() {
    FadeHost host;
    screen_fade().update(host);
}

void run_cutscene_borders() {
    BordersHost host;
    cutscene_borders().update(host);
}

}  // namespace sh::screens
