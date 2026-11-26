// The game's room and map load on the engine (src/engine/gameflow/area_load.h section
// 9): the door's sound set, the out-fade, the arrival point kept in the map event state until the load screen places
// the player, and the map or room change.

#include <cstdint>
#include <ctime>

#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/area_load.h"
#include "engine/gameflow/game/game_link.h"
#include "engine/gameflow/map_event_state.h"
#include "engine/log/sh_log.h"
#include "engine/player/player.h"

extern "C" {
std::int32_t GameFlow_DoorOpenSound(std::int32_t set);
unsigned int GameFlow_DoorStartsLoading(std::int32_t set);
void Chara_PositionSet(const void* mapPoint);

// Transition timing probe ([TXNTIME] log) and the door out-fade rate, 1/seconds in Q12 (Q12 1.6 ~= 0.63 s; console
// TXNFADE).
int g_PcTxnStartMs = 0;
int g_PcTxnFadeEndMs = 0;
std::int32_t g_PcTxnFadeTimestep = 6553;
}

namespace {

namespace gf = sh::gameflow;
namespace link = sh::gameflow::game;
namespace gs = sh::game_state;
using sh::game::PlayerVar;

constexpr std::int32_t kStateLoadMap = 5;
constexpr std::int32_t kMainLoadScreen = 10;

// The arrival point can be zeroed between the load and the player placement on PC (the map load runs through more
// subsystems than on the PS1); it is kept here and restored before use.
gf::MapPointRecord g_arrival_backup;
bool g_arrival_saved = false;

int now_ms() { return static_cast<int>(clock() * 1000 / CLOCKS_PER_SEC); }

class AreaLoadGameHost final : public gf::AreaLoadHost {
public:
    bool can_load() const override { return gf::map_event_state().event != nullptr && link::map_points_present(); }

    void door_opened() override {
        sh::game::session().system.scene.sfx_pair = event().sfx_pair;
        gs::sound_command(GameFlow_DoorOpenSound(sh::game::session().system.scene.sfx_pair));
    }
    bool fade_done() const override { return gs::screen_fade_finished(); }
    void start_out_fade() override {
        // Continue an in-flight fade from its level, and keep a scene's white fade white.
        gs::screen_fade_start(false, false, link::screen_fade_white());
        gs::set_screen_fade_timestep(g_PcTxnFadeTimestep);
    }
    void fade_progress() override { g_PcTxnFadeEndMs = now_ms(); }
    std::int32_t raw_delta() const override { return gs::delta_time_raw(); }
    void set_delta(std::int32_t d) override { link::set_delta_time(d); }

    void begin_load() override {
        SH_DBG("[TXNTIME] fade done, blocking work begins (fade took %d ms)", g_PcTxnFadeEndMs - g_PcTxnStartMs);
        gf::MapEventState& s = gf::map_event_state();
        const link::EventInfo e = event();
        auto& scene = sh::game::session().system.scene;
        sh::game::session().system.process.player_ready_mark = 0;
        // The loading screen of the arrival point the previous door chose.
        scene.loading_screen = static_cast<std::int8_t>(s.arrival.loading_screen());
        scene.sfx_pair = e.sfx_pair;
        scene.area_transition_flags = static_cast<std::int8_t>(e.options);
        if (GameFlow_DoorStartsLoading(scene.sfx_pair)) {
            s.load_flag_ticks = 0;
            sh::game::session().system.process.sys_flags |= sh::game::sys_flag::kLoading;
        }
        s.arrival = link::map_point(e.param);
    }
    gf::ArrivalPoint arrival_target() const override {
        const gf::MapPointRecord& a = gf::map_event_state().arrival;
        gf::ArrivalPoint p;
        p.x = a.x;
        p.z = a.z;
        p.heading_q8 = a.param0();
        p.half_width_q4 = a.param1();
        return p;
    }
    gf::ArrivalPoint door_point() const override {
        gf::ArrivalPoint p;
        // A map change a script asked for (load_map, sh.map_event_set: its event record requires flag -1) has no
        // door: the player keeps no offset from one, and lands on the point itself.
        if (event().required_flag == -1) {
            p.x = player_x();
            p.z = player_z();
            return p;
        }
        const gf::MapPointRecord d = link::map_point(event().point);
        p.x = d.x;
        p.z = d.z;
        return p;
    }
    std::int32_t player_x() const override { return static_cast<std::int32_t>(sh::game::player().get(PlayerVar::PositionX)); }
    std::int32_t player_z() const override { return static_cast<std::int32_t>(sh::game::player().get(PlayerVar::PositionZ)); }
    void set_arrival(const gf::ArrivalPoint& p) override {
        gf::MapEventState& s = gf::map_event_state();
        s.arrival.x = p.x;
        s.arrival.z = p.z;
        g_arrival_backup = s.arrival;
        g_arrival_saved = true;
    }
    gf::AreaTarget target() const override {
        // Read before the map load: it unloads the overlay that holds the event.
        const link::EventInfo e = event();
        gf::AreaTarget t;
        t.load_map = sh::game::session().system.flow.sys_state == kStateLoadMap;
        t.map = e.map;
        t.point = e.param;
        t.done_flag = e.done_flag;
        t.camera_rail = e.camera_rail;
        return t;
    }
    void load_map(std::int32_t map) override {
        sh::game::session().system.process.process_flags = sh::game::process_flag::kOverlayTransition;
        link::set_saved_map(map);
        link::boot_map_load(link::saved_map());
    }
    void change_room(std::int32_t map, int point) override {
        sh::game::session().system.process.process_flags = sh::game::process_flag::kRoomTransition;
        link::music_track_change(map);
        const int weather = link::map_point(point).weather_on_enter();
        if (weather != 0) {
            sh::game::session().system.particles.density_mode = static_cast<std::int8_t>(weather - 1);
        }
    }
    void set_flag(std::int16_t flag) override { gs::set_event_flag(flag, true); }
    void set_camera_rail(bool on) override { sh::game::session().set_sys_flag(sh::game::sys_flag::kOnCameraRail, on); }
    void start_load_screen() override {
        sh::game::session().system.process.music_status |= sh::game::music_status::kPause;
        sh::game::session().set_game_state_next(kMainLoadScreen);
        link::background_motion_blur_now();
    }

private:
    static link::EventInfo event() { return link::map_event(gf::map_event_state().event); }
};

const gf::AreaLoad& area_load() {
    static const gf::AreaLoad a = [] {
        gf::AreaLoad made;
        gf::AreaLoadRules r;
        r.fade_budget = 3 * 4096;
        r.min_step = link::kTimestep60Fps;
        made.set_rules(r);
        return made;
    }();
    return a;
}

}  // namespace

extern "C" void SysState_LoadArea_Update(void) {
    AreaLoadGameHost host;
    area_load().frame(host);
}

extern "C" void AreaLoad_UpdatePlayerPosition(void) {
    gf::MapEventState& s = gf::map_event_state();
    if (g_arrival_saved && s.arrival.x == 0 && s.arrival.z == 0 && (g_arrival_backup.x != 0 || g_arrival_backup.z != 0)) {
        SH_DBG("[TRANSITION] arrival point was zeroed, restoring x=%d z=%d", g_arrival_backup.x, g_arrival_backup.z);
        s.arrival = g_arrival_backup;
    }
    g_arrival_saved = false;
    Chara_PositionSet(&s.arrival);
}
