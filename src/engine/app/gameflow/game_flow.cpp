// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game's boot-time services (game_flow.h): title files by disc path through the file queue, the settings on the
// session, and the warm reset's host.

#include "engine/app/gameflow/game_flow.h"

#include <array>
#include <string>
#include <vector>

#include "engine/app/game_view.h"
#include "engine/app/gameflow/demo_host.h"
#include "engine/app/loader/game_files.h"
#include "engine/app/loop/frame_loop.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/warm_boot.h"
#include "engine/gconfig/controller_layouts.h"
#include "engine/memory/work_buffers.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/log/sh_log.h"
#include "engine/screens/screens_view.h"
#include "engine/timing/vblank.h"
#include "ui/pause/pause_menu.h"
#include "ui/screens/title_style.h"
#include "render/gpu/render_state.h"

// The freeze frame of pause, map messages and pickups (the renderer and the main loop).
extern int g_PcFreezeReleasePending;
// The blood colour the effect textures re-apply on a map load.
extern unsigned char g_PcTrustedBloodColor;

namespace sh::app {
namespace {

using sh::game::session;

void* const kOverlayBuffer = sh::memory::work_buffers().data(sh::memory::WorkBuffer::FileStaging);
constexpr int kVolumeMax = 128;
constexpr int kVibrationOn = 128;
constexpr int kAudioStereo = 2;
// The fade starts from black at once, then fades in (reset, fade in, black).
constexpr std::int32_t kFadeInFromBlack = 6;

std::int32_t file(const char* path) { return sh::loader::disc_files().find(path); }

std::array<sh::gconfig::ControllerLayout, sh::gconfig::kLayoutTypeCount> s_layouts;
bool s_layouts_loaded = false;

class SessionWarmBootHost final : public sh::gameflow::WarmBootHost {
public:
    void presented_frame_release() override
    {
        // A warm reset can fire from any state that holds the freeze frame: pause, the map-screen messages and the
        // item pickup re-arm it every tick and only drop it on their own exit path, which a reset never takes. Left
        // set, the title would come up drawn over the last frame of play.
        sh::render::feedback_control().present_last_frame = 0;
        g_PcFreezeReleasePending = 0;
    }
    void pause_menu_reset() override { sh::ui::pause_menu().reset(); }
    void draw_sync() override { sh::screens::wait_frame_done(false); }
    void screen_clear() override { view::clear_screen_rect(0, 32, 512, 448); }
    void vibration_effect(int effect) override { view::vibration_play(effect); }
    void vibration_pump() override { view::vibration_update(); }
    void audio_call(int command) override { sh::game_state::sound_command(command); }
    bool audio_streaming() const override { return view::audio_streaming(); }
    void audio_tasks_run() override { view::audio_tasks_run(); }
    void vsync_wait() override { sh::timing::wait_vblank(); }
    void queue_reset() override { sh::loader::queue_reset(); }
    void queue_wait_empty() override { sh::loader::queue_wait_empty(); }
    void audio_work_init() override { view::audio_work_init(); }
    void ambient_set(int mode) override { view::ambient_set(mode); }
    void demo_stop() override { sh::gameflow::demo_player().stop(sh::gameflow::demo_host()); }
    void demo_sequence_advance(int step) override { sh::gameflow::demo_player().advance(step); }
    void demo_data_read() override { sh::gameflow::demo_player().read_setup(sh::gameflow::demo_host()); }
    void title_graphics_load() override { sh::app::title_graphics_load(); }
    void pad_update() override { sh::loop::pad_update(); }
    void fade_out_start() override { sh::screens::set_fade_status(kFadeInFromBlack); }
    void fade_timestep_zero() override { sh::screens::set_fade_timestep(0); }
};

}  // namespace

void title_graphics_seek()
{
    // The Japanese title picture (TIM/TITLE.TIM: same shape and descriptor as the US TITLE_E, other artwork) when
    // the style module asks for it; everything downstream (draw, menu, fog) is the same.
    sh::loader::queue_seek(file(sh::ui::title_art().is_japanese_picture() ? "TIM/TITLE.TIM" : "TIM/TITLE_E.TIM"));
}

void title_graphics_load()
{
    // Three title screens, none of which every disc has; the style module decides which to show and where from.
    sh::ui::title_art().load();
}

void stream_overlay_seek() { sh::loader::queue_seek(file("VIN/STREAM.BIN")); }
void stream_overlay_load() { sh::loader::queue_read(file("VIN/STREAM.BIN"), kOverlayBuffer); }

void option_overlay_load()
{
    sh::loader::queue_read_image(file("TIM/OPTION.TIM"), kOverlayBuffer, view::picture(view::Picture::ItemInspection));
    sh::loader::queue_read(file("VIN/OPTION.BIN"), kOverlayBuffer);
}

void save_overlay_load()
{
    sh::loader::queue_read_image(file("TIM/SAVELOAD.TIM"), kOverlayBuffer, view::picture(view::Picture::ItemInspection));
    sh::loader::queue_read(file("VIN/SAVELOAD.BIN"), kOverlayBuffer);
}

void settings_apply_screen_and_volume()
{
    const sh::game::Settings& s = session().game.settings;
    view::screen_position_set(s.screen_x, s.screen_y);
    // Internal mixing is always stereo: the speaker layout (mono to 7.1) is the speaker_layout option, and the
    // console's mono mode would defeat the surround pipeline.
    sh::game_state::sound_command(kAudioStereo);
    sh::screens::set_sound_volumes(kVolumeMax, s.music_volume, s.sfx_volume);
}

void settings_restore_defaults()
{
    sh::game::Settings& s = session().game.settings;
    s.weapon_control_press = 1;
    s.brightness = 3;
    settings_restore_control_defaults(0);
    s.vibration = kVibrationOn;
    s.music_volume = kVolumeMax;
    s.sfx_volume = kVolumeMax;
    settings_apply_screen_and_volume();
    s.blood_color = 0;
    g_PcTrustedBloodColor = 0;
}

void settings_restore_control_defaults(int layout)
{
    if (layout < 0 || layout >= static_cast<int>(sh::gconfig::kLayoutTypeCount) || !s_layouts_loaded) return;
    std::uint16_t* binds = &session().game.settings.binds.enter;
    for (std::size_t i = 0; i < sh::gconfig::kLayoutActionCount; i++) binds[i] = s_layouts[layout].masks[i];
}

void warm_boot()
{
    static SessionWarmBootHost host;
    sh::gameflow::WarmBoot(host).run();
}

}  // namespace sh::app

extern "C" void ControllerLayouts_GamedataInit(const char* gamedataDir, const char* modsDir);

// `mods reload`: the default layouts read again (the player's binds stay as they are).
extern "C" void ControllerLayouts_GamedataReload(const char* gamedataDir, const char* modsDir)
{
    sh::app::s_layouts_loaded = false;
    ControllerLayouts_GamedataInit(gamedataDir, modsDir);
}

// Loaded once at boot (game_main.cpp), before the settings are first restored.
extern "C" void ControllerLayouts_GamedataInit(const char* gamedataDir, const char* modsDir)
{
    using namespace sh::gconfig;
    if (sh::app::s_layouts_loaded || gamedataDir == nullptr) return;

    std::vector<std::string> warnings;
    ControllerLayoutLoadSource source;
    auto library = ControllerLayoutLibrary::load(gamedataDir, modsDir ? modsDir : "", warnings, &source);
    if (!library) {
        SH_ERR("[LAYOUTS] %s (controller defaults stay unset)", library.error().c_str());
        return;
    }
    for (const std::string& warning : warnings) SH_WARN("[LAYOUTS] %s", warning.c_str());

    const auto* region = library.value().region("usa");
    if (region == nullptr) {
        SH_ERR("[LAYOUTS] controller_layouts.json has no layouts for this region");
        return;
    }
    sh::app::s_layouts = *region;
    sh::app::s_layouts_loaded = true;
    SH_LOG("[LAYOUTS] gamedata/config: %d layout types%s", static_cast<int>(kLayoutTypeCount),
           source.mod_id.empty() ? "" : (" (mod " + source.mod_id + ")").c_str());
}
