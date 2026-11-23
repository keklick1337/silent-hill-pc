// The game's in-game state machine on the engine (src/engine/gameflow/game_states.h
// section 9): GameState_InGame_Update runs the engine's state dispatch and then the world update of the frame; the
// gameplay state asks the engine what follows a frame; the event states (set flag, play sound, event script), the
// message state and the event screen game state run through GameStateHost; the menu states (pause, options, items,
// map, movie, save: menu_states.h) through GameMenuHost. The room and map load and the game over have their own
// entry points (area_load_host.cpp, game_over_host.cpp).

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

#include "engine/app/pc_paths.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/flow_data.h"
#include "engine/gameflow/game/game_link.h"
#include "engine/gameflow/game/step_hosts.h"
#include "engine/gameflow/game_states.h"
#include "engine/gameflow/map_event_state.h"
#include "engine/gameflow/menu_states.h"
#include "engine/gameflow/world_frame.h"
#include "engine/ids/game_numbers.h"
#include "engine/log/sh_log.h"
#include "engine/player/player.h"
#include "engine/screens/paper_map_view.h"
#include "engine/screens/screens_view.h"
#include "engine/text/game_text.h"

extern "C" {
void Event_Update(unsigned int disableButtonEvents);
void SysWork_SavegameUpdatePlayer(void);
void SysState_LoadArea_Update(void);
void SysState_GameOver_Update(void);
void GameState_ControllerVibrationUpdate(void);
void Game_NpcRoomInitSpawn(unsigned int cond);
void Game_NpcUpdate(void);
void Radio_NpcInfoClear(void);
void World_NearbyPlayerCollisionTriggersGet(void);
void Vibration_Play(std::int32_t step);
std::int32_t Vibration_Update(void);
}

namespace {

namespace gf = sh::gameflow;
namespace link = sh::gameflow::game;
using sh::game::PlayerVar;
namespace gs = sh::game_state;

// Game data of the state machine.
constexpr std::int32_t kEventSoundBase = gs::kSfxBase;
constexpr int kMapEventScripts = 64;  // the overlays' script tables are shorter; see run_event_script
constexpr int kStateCount = 15;
constexpr std::int32_t kStateLoadMap = 5;
constexpr std::int32_t kStateLoadRoom = 6;
constexpr std::int32_t kStateMapScreen = 3;
constexpr std::int32_t kStateGameOver = 13;
constexpr std::int32_t kStatePaused = 14;
constexpr std::int32_t kBordersNone = 1;
constexpr std::uint32_t kMusicStatusRadio = 1u << 6;  // set when the in-game state starts; the music mixer's radio bit

// Q12 numbers.
constexpr std::int32_t kFadeRateGameStart = 3 * 4096;
constexpr std::int32_t kNoGround = 8 * 4096;

// Activation of an event by the action button (the event record's activation type).
constexpr int kActivationButton = 2;

// The game's message numbers, save point and flags the menu states use.
gf::MenuStateRules menu_rules() {
    gf::MenuStateRules r;
    r.pause_on_sound = 3;
    r.pause_off_sound = 4;
    r.stop_stream_sound = 19;
    r.no_map_message = 3;
    r.too_dark_message = 4;
    r.save_prompt_message = 2;
    r.save_seen_flag = static_cast<std::int16_t>(std::max(sh::ids::flag_number("SeenSaveScreen"), 0));
    r.save_without_prompt = 24;
    r.movie_resumes_game = 1 << 1;
    return r;
}

// The world frame's rules: gamedata/config/world_frame.json, else the built-in ones (the same numbers).
const gf::WorldFrameRules& world_rules() {
    static const gf::WorldFrameRules* rules = nullptr;
    if (rules == nullptr) {
        char mods[1024];
        PcPaths_ExeFile(mods, sizeof(mods), "mods");
        std::vector<std::string> warnings;
        std::string source;
        const sh::Result<std::string> text =
            gf::read_config_file(link::gamedata_path(), mods, "world_frame.json", &source, warnings);
        gf::WorldFrameRules chosen = gf::WorldFrameRules::builtin();
        if (text) {
            sh::Result<gf::WorldFrameRules> parsed = gf::WorldFrameRules::parse(text.value());
            if (parsed) {
                chosen = std::move(parsed.value());
            } else {
                SH_ERR("[GAMEFLOW] %s (built-in world frame rules)", parsed.error().c_str());
            }
        } else {
            SH_ERR("[GAMEFLOW] config/world_frame.json: %s (built-in world frame rules)", text.error().c_str());
        }
        rules = new gf::WorldFrameRules(std::move(chosen));
    }
    return *rules;
}

gf::MenuStates& menu_states() {
    static gf::MenuStates m = [] {
        gf::MenuStates made;
        made.set_rules(menu_rules());
        return made;
    }();
    return m;
}

gf::RunningEvent running_event_now() {
    gf::RunningEvent ev;
    const gf::MapEventState& s = gf::map_event_state();
    ev.param = static_cast<int>(s.param);
    if (s.event != nullptr) {
        const link::EventInfo e = link::map_event(s.event);
        ev.present = true;
        ev.button = e.activation == kActivationButton;
        ev.done_flag = e.done_flag;
        ev.options = static_cast<std::uint8_t>(e.options);
    }
    return ev;
}

const sh::game::PadState& pad() { return sh::game::session().game.pads[0]; }
const sh::game::PadBinds& binds() { return sh::game::session().game.settings.binds; }

// A scene where the flashlight can be used, with it off and both light sources dark.
bool too_dark() {
    const auto& env = sh::game::session().system.environment;
    return (env.current.preset.flags & (1 << 1)) && !env.flashlight_on &&
           ((env.transition[0].preset.flags & (1 << 0)) || (env.transition[1].preset.flags & (1 << 0)));
}

void open_game_state(int state) { sh::game::session().set_game_state_next(state); }

class GameMenuHost final : public link::SessionSteps<gf::MenuStateHost> {
public:
    gf::RunningEvent running_event() const override { return running_event_now(); }
    void set_flag(std::int16_t flag) override { gs::set_event_flag(flag, true); }

    void hold_world() override {
        // MainLoop clears the pause bit every frame; the world update (objects, triggers, player) is gated on it.
        // The renderer re-presents the captured last gameplay frame under the state's own drawing.
        sh::game::session().system.process.music_status |= sh::game::music_status::kPause;
        link::hold_last_frame();
    }
    void release_world() override { link::release_frozen_frame(); }
    bool screen_ready() const override { return gf::map_event_state().screen_ready != 0; }
    void clear_screen_ready() override { gf::map_event_state().screen_ready = 0; }
    bool chunks_idle() override { return link::map_chunks_idle(); }
    void sound_command(int command) override { gs::sound_command(command); }
    void fade_out(bool restart) override { gs::screen_fade_start(restart, false, false); }
    void fade_in() override { gs::screen_fade_start(true, true, false); }
    void fade_instant() override { gs::set_screen_fade_timestep(0); }
    void open_screen(gf::MenuScreen screen) override {
        switch (screen) {
        case gf::MenuScreen::Options:
            open_game_state(static_cast<int>(gs::GameState::OptionScreen));
            break;
        case gf::MenuScreen::Inventory:
            open_game_state(static_cast<int>(gs::GameState::LoadStatusScreen));
            break;
        case gf::MenuScreen::PaperMap:
            open_game_state(static_cast<int>(gs::GameState::PaperMapScreen));
            break;
        case gf::MenuScreen::Save:
            open_game_state(static_cast<int>(gs::GameState::SaveScreen));
            break;
        case gf::MenuScreen::InGame:
            open_game_state(static_cast<int>(gs::GameState::InGame));
            break;
        }
    }

    bool native_pause_menu() const override { return link::pause_menu_native(); }
    void draw_paused() override { link::draw_paused_text(); }
    void paused_extras() override {
        // The clear-time clock runs on while paused, as on PSX (pause_menu = 1 stops it in its own menu).
        link::game_timer_update();
    }
    bool pause_pressed() const override { return (pad().clicked & binds().pause) != 0; }
    bool debug_save_combo() const override {
        // D-pad left + L2 + L1 + left stick left + right stick left, then L3.
        constexpr std::uint32_t kL3 = 1u << 1;
        constexpr std::uint32_t kCombo = kL3 | (1u << 7) | (1u << 8) | (1u << 10) | (1u << 19) | (1u << 23) | (1u << 27);
        return pad().held == kCombo && (pad().clicked & kL3) != 0;
    }
    void clear_event_param() override { gf::map_event_state().param = 0; }
    void reset_pause_menu() override { link::pause_menu_reset(); }
    void update_pause_menu() override {
        // The menu Back opens is no gameplay change: the clear-time clock runs on as under PAUSED.
        if (!link::pause_menu_stops_clock()) {
            link::game_timer_update();
        }
        link::pause_menu_update();
    }
    bool pause_menu_return_pending() const override { return link::pause_menu_return_pending(); }

    void load_options_screen() override { link::load_options_screen(); }

    gf::MapBlock map_block() const override {
        if (!gs::paper_map_held(gs::save_paper_map())) {
            return gf::MapBlock::NoMap;
        }
        if (too_dark()) {
            return gf::MapBlock::TooDark;
        }
        return gf::MapBlock::None;
    }
    bool map_pressed() const override { return (pad().clicked & binds().map) != 0; }
    void load_paper_map() override {
        const int map = gs::save_paper_map();
        if (sh::screens::paper_map_marking_file(map) >= 0) {
            link::read_image(sh::screens::paper_map_marking_file(map), link::Picture::PaperMapMarkings);
        }
        link::seek_file(sh::screens::paper_map_image_file(map));
    }

    void load_movie_stream() override { link::load_movie_stream(); }
    void play_movie(int param) override {
        // The movie draws over texture memory: the visible frame is kept and put back afterwards.
        link::keep_movie_area();
        Vibration_Play(0);
        Vibration_Update();
        // The movie file is counted down from the last XA file by the event parameter.
        link::play_event_movie(param);
        Vibration_Play(1);
        link::restore_movie_area();
    }

    void poll_saves() override { link::poll_saves(); }
    void store_player() override { SysWork_SavegameUpdatePlayer(); }
    int save_location() const override { return link::saved_player().location; }
    void load_save_screen() override { gs::load_save_screen_files(); }
    void stop_rumble() override { GameState_ControllerVibrationUpdate(); }
};

void gameplay_frame();

void log_script(int param) {
    static int last[4] = {-1, -1, -1, -1};
    const auto& steps = sh::game::session().system.flow.sys_steps;
    const int now[4] = {param, static_cast<int>(steps[0]), static_cast<int>(steps[1]), static_cast<int>(steps[2])};
    if (now[0] != last[0] || now[1] != last[1] || now[2] != last[2] || now[3] != last[3]) {
        SH_DBG("[SS] event script param=%d step0=%d step1=%d step2=%d", now[0], now[1], now[2], now[3]);
        for (int i = 0; i < 4; ++i) {
            last[i] = now[i];
        }
    }
}

class GameStateHost final : public gf::StateHost {
public:
    bool player_alive() const override { return sh::game::player().get(PlayerVar::Health) > 0; }

    std::int32_t delta() const override { return gs::delta_time(); }
    std::int32_t raw_delta() const override { return gs::delta_time_raw(); }
    void set_delta(std::int32_t d) override { link::set_delta_time(d); }

    void run_gameplay() override { gameplay_frame(); }

    bool run_menu_state(gf::GameState s) override {
        GameMenuHost menus;
        menu_states().run(s, menus);
        return true;
    }

    void run_state(gf::GameState s) override {
        static int logged = -1;
        if (static_cast<int>(s) != logged) {
            SH_DBG("[SYSSTATE] dispatch sysState=%d mapEventData=%p", static_cast<int>(s), gf::map_event_state().event);
            logged = static_cast<int>(s);
        }
        // The states with their own entry point; the others run in the state machine.
        switch (static_cast<std::int32_t>(s)) {
        case kStateLoadMap:
        case kStateLoadRoom:
            SysState_LoadArea_Update();
            break;
        case kStateGameOver:
            SysState_GameOver_Update();
            break;
        default:
            break;
        }
    }

    gf::RunningEvent running_event() const override { return running_event_now(); }

    gf::GameState scan_events() override {
        Event_Update(true);
        return static_cast<gf::GameState>(static_cast<std::uint8_t>(gf::map_event_state().sys_state));
    }

    void before_event_state(gf::GameState s) override {
        // Door out-fade: see the note in gameplay_frame.
        if (gf::is_area_load(s)) {
            link::arm_feedback_store();
        }
    }

    void set_flag(std::int16_t flag) override { gs::set_event_flag(flag, true); }
    void play_sound(std::int32_t sound) override { gs::sound_command(sound); }

    bool event_script_allowed(int param) override { return link::hook_map_event_allowed(param); }

    bool run_event_script(int param) override {
        // A mod's runtime pickup runs the game's item prompt under a synthetic event record.
        if (link::runtime_pickups_run_event()) {
            return true;
        }
        // The overlays' tables have no length (map0_s02 has 7); a stale parameter must not call past the end.
        if (param < 0 || param >= kMapEventScripts || !link::overlay_event_script_present(param)) {
            SH_DBG("[SS] event script param=%d out of range or empty, skipped", param);
            return false;
        }
        log_script(param);
        // A Lua port in maps/<map>/events.lua may own the handler (handlers.json names it).
        if (!link::script_map_handler(param)) {
            link::overlay_event_script(param);
        }
        return true;
    }

    bool enemy_alive() const override { return link::hostile_npc_alive(); }

    void freeze_player() override { link::overlay_freeze_player(); }
    void unfreeze_player() override { link::overlay_unfreeze_player(); }
    gf::MessageResult show_message(int message) override {
        return static_cast<gf::MessageResult>(link::map_message_draw(message));
    }

    void start_event_screen() override {
        link::set_interval_vblanks(1);
        gs::screen_fade_start(true, true, false);
        sh::game::session().game.game_steps[0] = 1;
    }
    void update_ready() override {
        gf::map_event_state().screen_ready = gs::screen_fade_finished() && link::file_chunks_loaded();
    }
    void draw_event_screen() override { link::draw_picture(link::Picture::ItemInspection); }
};

gf::StateMachine& machine() {
    static gf::StateMachine m = [] {
        gf::StateMachine made;
        gf::StateRules rules;
        rules.sound_base = kEventSoundBase;
        rules.state_count = kStateCount;
        made.set_rules(rules);
        return made;
    }();
    return m;
}

// The gameplay decision's inputs.
void gameplay_input(gf::GameplayInput& in) {
    in.alive = sh::game::player().get(PlayerVar::Health) > 0;
    in.light_pressed = (pad().clicked & binds().light) != 0;
    in.light_available = (sh::game::session().system.environment.current.preset.flags & (1 << 1)) != 0;
    in.state = static_cast<gf::GameState>(static_cast<std::uint8_t>(sh::game::session().system.flow.sys_state));
    in.map_event = static_cast<gf::GameState>(static_cast<std::uint8_t>(gf::map_event_state().sys_state));
    in.pause_pressed = (pad().clicked & binds().pause) != 0;
    in.attacking = sh::game::player().attacking();
    in.status_pressed = (pad().clicked & binds().item) != 0;
    in.map_pressed = (pad().clicked & binds().map) != 0;
    in.options_pressed = (pad().clicked & binds().option) != 0;
    in.fade_idle = link::screen_fade_none();
}

void gameplay_frame() {
    Event_Update(sh::game::player().get(PlayerVar::AttackReceived) != -1);
    link::map_room_update();

    const std::int32_t health = static_cast<std::int32_t>(sh::game::player().get(PlayerVar::Health));
    // Health in tenths of the full Q12 unit, rounded up.
    const std::int32_t rumble = world_rules().rumble_for((health + (4096 * 10 - 1)) / (4096 * 10));
    if (rumble >= 0) {
        Vibration_Play(rumble);
    }

    if (health <= 0) {
        return;
    }

    gf::GameplayInput in;
    gameplay_input(in);

    const gf::GameplayDecision d = gf::decide_gameplay(in);
    if (d.toggle_light) {
        link::flashlight_toggle();
    }

    if (d.next != gf::GameState::None) {
        if (d.next == in.map_event) {
            SH_DBG("[GP-STATE-TXN] Gameplay -> sysState=%d (param=%d)", static_cast<int>(gf::map_event_state().sys_state),
                   static_cast<int>(gf::map_event_state().param));
            // Door out-fade. Retail left the last gameplay frame in the other framebuffer and decayed it under the
            // load; the renderer blanks both display rects unless the store is armed for this frame's present,
            // which is the last one that still renders the world.
            if (gf::is_area_load(d.next)) {
                link::arm_feedback_store();
                link::set_transition_start_ms(static_cast<int>(clock() * 1000 / CLOCKS_PER_SEC));
            }
        } else if (d.next == gf::GameState::Paused) {
            // The pause state arms the frozen-frame present itself on its first frame; only a pending release is
            // cleared here.
            link::cancel_frozen_frame_release();
            link::pause_menu_enter();
        }
        sh::game::session().set_sys_state_next(static_cast<std::int32_t>(d.next));
        if (d.clear_message) {
            sh::game::session().system.flow.message_shown = 0;
        }
    }

    if (d.menu == gf::MenuFlag::Set) {
        sh::game::session().system.process.sys_flags |= sh::game::sys_flag::kMenu;
    } else if (d.menu == gf::MenuFlag::Clear) {
        sh::game::session().system.process.sys_flags &= ~sh::game::sys_flag::kMenu;
    }
}

// The world update that follows the state of the frame, as the engine orders it.
class WorldFrame final : public gf::WorldFrameHost {
public:
    void latch_chunks_ready() override {
        gf::map_event_state().screen_ready = gs::screen_fade_finished() && link::file_chunks_loaded();
    }
    void update_world_objects() override {
        link::overlay_update_world_objects();
        link::runtime_pickups_draw();
    }
    void cutscene_camera() override { sh::screens::run_cutscene_borders(); }
    void crosshair() override { link::crosshair_draw(); }
    void music_track() override { link::music_track_update(); }
    void restore_random() override { link::demo_random_restore(); }
    void collision_triggers() override { World_NearbyPlayerCollisionTriggersGet(); }
    void move_camera() override { link::camera_move_and_set(); }
    void alternate_cameras() override { link::debug_camera_update(); }
    void map_frame_hook() override {
        // map4_s03's TV bank projects its screen quads here, after the alternate cameras.
        link::overlay_frame_hook();
    }
    void update_player() override { sh::game::player().tick(); }
    void update_flashlight() override { link::flashlight_update(); }
    std::string map_name() override {
        const char* name = gs::map_name(link::saved_map());
        return name != nullptr ? name : "";
    }
    void update_particles() override { link::overlay_particles_update(link::saved_map()); }
    bool player_visible() override { return link::player_visible(); }
    void draw_player() override { link::draw_player(); }
    void npc_spawn_pass() override { Game_NpcRoomInitSpawn(true); }
    void update_npcs() override { Game_NpcUpdate(); }
    void update_2d_effects() override { link::effects_2d_update(); }
    void close_range_chunks() override { link::close_range_chunks_init(); }
    void sustain_positional_loops() override { link::sustain_positional_loops(); }
    void draw_world() override { link::draw_world(); }
    void frame_world() override {
        // Hor+ here rather than in the main loop's gate (which runs earlier and would lag the inventory exit by two 4:3
        // frames). The map screen and the paper-map/pickup protect flag still win; the ending raises that flag
        // during a 3D cutscene.
        const bool cutscene_live = sh::game::session().sys_flag(sh::game::sys_flag::kCutscene) ||
                                   sh::game::session().system.flow.cutscene_border != kBordersNone;
        link::frame_world_widescreen(cutscene_live);
    }
    void advance_random() override { link::demo_random_advance(); }
};

void world_frame() {
    WorldFrame host;
    gf::run_world_frame(host, world_rules());
}

}  // namespace

extern "C" void GameState_InGame_Update(void) {
    link::demo_random_backup();

    switch (sh::game::session().game.game_steps[0]) {
    case 0:
        gs::screen_fade_start(true, true, false);
        gs::set_screen_fade_timestep(kFadeRateGameStart);
        sh::game::session().game.game_steps[0] = 1;
        [[fallthrough]];

    case 1: {
        link::wait_draw();
        Radio_NpcInfoClear();
        link::map_room_update();
        Vibration_Play(1);

        // Door triggers and spawn points zero Harry's Y, and the boot's height query may run before he reaches his
        // final spawn: snap Y to the ground once so the first movement tick does not read a stale height.
        sh::game::Player& player = sh::game::player();
        const std::int32_t ground = gs::ground_height(static_cast<std::int32_t>(player.get(PlayerVar::PositionX)),
                                                      static_cast<std::int32_t>(player.get(PlayerVar::PositionZ)));
        if (ground != kNoGround) {
            player.set(PlayerVar::PositionY, ground);
            player.set(PlayerVar::GroundHeight, ground);
        }

        link::set_interval_vblanks(2);
        sh::game::session().game.game_steps[0]++;
        sh::game::session().system.process.music_status |= kMusicStatusRadio;
        break;
    }
    }

    // Pause left by a route other than its own exits (death, the debug save combo): the next pause opens fresh and
    // captures again.
    if (sh::game::session().system.flow.sys_state != kStatePaused && !link::pause_menu_return_pending()) {
        link::pause_menu_reset();
    }

    GameStateHost host;
    machine().frame(host);

    link::demo_random_restore();
    world_frame();
}

extern "C" void GameState_MapEvent_Update(void) {
    GameStateHost host;
    machine().event_screen_frame(host);
}
