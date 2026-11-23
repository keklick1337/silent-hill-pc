// SPDX-License-Identifier: GPL-3.0-or-later
//
// The map load machine on the session (sh::gameflow::MapLoad; numbers in
// gamedata/config/map_load.json): its host over the file queue, the demo player, the fade and the systems the
// game view reaches. The load screen's tunables (console TXNHOLD, LOADPACE, LOADMIN; config load_screen_min) and its
// clock are read by name across the engine.

#include <cstdint>
#include <string>
#include <vector>

#include "engine/app/game_view.h"
#include "engine/app/gameflow/demo_host.h"
#include "engine/app/gameflow/game_flow.h"
#include "engine/app/loader/game_files.h"
#include "engine/app/pc_paths.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/flow_data.h"
#include "engine/gameflow/map_load.h"
#include "engine/log/sh_log.h"
#include "engine/loop/frame_view.h"
#include "engine/maps/map_runtime.h"
#include "engine/screens/screens_view.h"
#include "engine/timing/vblank.h"
#include "render/gpu/render_state.h"
#include "engine/mods/data_reload.h"
#include "input/object_pick.h"

extern int g_PcFastBlockingLoads;

extern "C" {
const char* PcPort_GetGameDataPath(void);
void Pc_3dAudio_SustainReset(void);
}

namespace {

namespace gf = sh::gameflow;
using sh::game::session;

constexpr int kSfxLoadScreenOpen = 1501;
constexpr int kSfxLoadScreenOpen2 = 1502;

class SessionMapLoadHost final : public gf::MapLoadHost {
public:
    bool event_flag(int index) const override { return (sh::app::view::map_event_flags() & (1 << index)) != 0; }

    int queue_length() const override { return sh::loader::queue_length(); }
    void queue_update() override { sh::loader::queue_update(); }
    void queue_drain_now() override
    {
        if (g_PcFastBlockingLoads) sh::loader::queue_drain_now();
    }
    bool audio_streaming() const override { return sh::app::view::audio_streaming(); }
    int vsync_count() override { return sh::timing::vblank_count(); }
    std::int32_t delta_time() const override { return sh::loop::frame_delta(); }
    void hold_feedback_frame() override { sh::render::feedback_control().hold_frame = 1; }

    void interval_vblanks_set(int vblanks) override { sh::loop::frame_interval_vblanks() = vblanks; }
    void area_player_position_update() override { sh::app::view::area_player_position_update(); }
    void audio_call(int sound) override { sh::game_state::sound_command(sound); }

    void demo_savegame_update() override { gf::demo_save_apply(); }
    void player_init() override { sh::app::view::player_init(); }
    bool demo_buffer_setup() override { return gf::demo_player().place_play_buffer(gf::demo_host()) != 0; }
    void demo_map_load() override { sh::app::view::map_load(sh::game_state::save_map_index()); }
    void demo_sequence_advance() override { gf::demo_player().advance(1); }
    void demo_data_read() override { gf::demo_player().read_setup(gf::demo_host()); }
    void demo_play_data_read() override { gf::demo_player().read_play(gf::demo_host()); }
    void demo_start() override { gf::demo_player().start(gf::demo_host()); }

    void loading_screen_draw(int fade_timestep) override
    {
        // Fade in without a reset of its progress (fade-out steps + fade in, black).
        sh::screens::set_fade_status(7);
        sh::screens::set_fade_timestep(fade_timestep);
        sh::app::view::loading_screen_run(session().system.scene.loading_screen);
    }
    void background_motion_blur() override { sh::app::view::background_motion_blur(); }
    void chara_group_alloc(int slot) override
    {
        sh::app::view::chara_anim_alloc(slot, sh::maps::map_runtime().group_chara(slot - 1));
    }
    void chara_groups_load() override { sh::app::view::map_chara_groups_load(); }
    void chara_groups_loaded() override
    {
        // Console scale is per NPC slot and prop placement and positional sounds belong to the old map: neither
        // survives a map change.
        sh::input::object_pick().reset();
        Pc_3dAudio_SustainReset();
    }
    void world_clear() override { sh::app::view::world_clear(); }
    void player_chunk_init() override { sh::app::view::player_chunk_init(); }
    void radio_stop() override { sh::screens::stop_radio_sound(); }
    bool chunks_ready() override { return sh::app::view::chunks_ready(); }
    int music_init() override { return sh::app::view::music_init(); }
    void audio_state_force_clear() override { sh::app::view::audio_state_force_clear(); }
    int ambient_init() override { return sh::app::view::ambient_init(); }
    void npc_init() override { sh::app::view::npc_init(); }
    void in_game_init() override { sh::app::view::in_game_init(); }
    void transition_sound() override { sh::app::view::transition_sound(); }
    void fade_reset_timestep() override
    {
        // Reset of the fade's step, keeping its colour.
        sh::screens::set_fade_status(4 | (sh::screens::fade_status() & 8));
    }

    int load_flag_tick() override { return sh::app::view::load_flag_tick(); }
    void load_done_sounds() override
    {
        sh::game_state::sound_command(kSfxLoadScreenOpen2);
        sh::game_state::sound_command(kSfxLoadScreenOpen);
    }
};

SessionMapLoadHost s_host;

// Tunables set before the data file is read (the configuration file loads first); -1 = not set.
int s_hold_override = -1;
int s_pace_override = -1;
int s_min_override = -1;

gf::MapLoad* s_machine = nullptr;

gf::MapLoad& machine()
{
    // `mods reload` reads the numbers again; the machine keeps its step in the session, so a new one carries on.
    static std::uint32_t s_epoch = sh::mods::kNeverRead;
    if (sh::mods::data_stale(s_epoch) && s_machine != nullptr) {
        delete s_machine;
        s_machine = nullptr;
    }
    if (s_machine == nullptr) {
        gf::MapLoadParams params;
        char mods[1024];
        PcPaths_ExeFile(mods, sizeof(mods), "mods");
        std::vector<std::string> warnings;
        std::string source;
        const sh::Result<std::string> text =
            gf::read_config_file(PcPort_GetGameDataPath(), mods, "map_load.json", &source, warnings);
        for (const std::string& w : warnings) SH_WARN("[MAPLOAD] %s", w.c_str());
        if (!text) {
            SH_ERR("[MAPLOAD] config/map_load.json: %s (built-in numbers)", text.error().c_str());
        } else {
            const sh::Result<gf::MapLoadParams> parsed = gf::MapLoadParams::parse(text.value());
            if (parsed)
                params = parsed.value();
            else
                SH_ERR("[MAPLOAD] config/map_load.json: %s (built-in numbers)", parsed.error().c_str());
        }
        if (s_hold_override >= 0) params.hold_frames = s_hold_override;
        if (s_pace_override >= 0) params.pace_vblanks = s_pace_override;
        if (s_min_override >= 0) params.min_vblanks = s_min_override;
        s_machine = new gf::MapLoad(s_host, params);
    }
    return *s_machine;
}

}  // namespace

// The getters return the number in force; the setters work before the machine exists.
extern "C" int Pc_LoadScreenHoldFrames(void) { return machine().params().hold_frames; }
extern "C" int Pc_LoadScreenPaceVblanks(void) { return machine().params().pace_vblanks; }
extern "C" int Pc_LoadScreenMinVblanks(void) { return machine().params().min_vblanks; }

extern "C" void Pc_LoadScreenHoldFramesSet(int v)
{
    s_hold_override = v < 0 ? 0 : v;
    if (s_machine != nullptr) s_machine->params().hold_frames = s_hold_override;
}

extern "C" void Pc_LoadScreenPaceVblanksSet(int v)
{
    s_pace_override = v < 0 ? 0 : v;
    if (s_machine != nullptr) s_machine->params().pace_vblanks = s_pace_override;
}

extern "C" void Pc_LoadScreenMinVblanksSet(int v)
{
    s_min_override = v < 0 ? 0 : v;
    if (s_machine != nullptr) s_machine->params().min_vblanks = s_min_override;
}

extern "C" std::int32_t Pc_LoadScreenDt(void) { return machine().screen_dt(); }

extern "C" void GameState_LoadMapScreen_Update(void) { machine().screen_update(); }
void sh::app::game_startup() { machine().startup(); }
