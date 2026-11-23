// The game's title menu on the engine (src/engine/gameflow/title_menu.h):
// GameState_MainMenu_Update runs the engine's title menu through TitleMenuHost, which reads the pad, the mouse, the
// saves and the settings and does the game's work (fades, file loads, the new game and continue boots, text
// drawing). The entries come from gamedata/menu/title_menu.json; the title picture and its fog are drawn here and
// in title_fog_host.cpp. Also the map warp (a New Game on a picked map).

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "engine/loader/map_list.h"
#include "engine/app/pc_paths.h"
#include "engine/config/pc_config.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/flow_data.h"
#include "engine/gameflow/game/game_link.h"
#include "engine/gameflow/map_event_state.h"
#include "engine/gameflow/title_menu.h"
#include "engine/gameflow/warp_story.h"
#include "engine/log/sh_log.h"
#include "engine/player/player.h"
#include "engine/text/game_text.h"
#include "i18n/localization.h"
#include "input/mouse_pointer.h"
#include "render/draw2d/draw2d.h"
#include "ui/menu/pc_debug_menu.h"
#include "ui/menu/nm_draw.h"
#include "ui/screens/title_style.h"
#include "engine/mods/data_reload.h"

extern "C" {
void Title_FogInit(void);
void MainMenu_FogUpdate(void);
void Chara_PositionSet(const void* mapPoint);
void TitleMenu_PollSaves(void);
void SysWork_SavegameUpdatePlayer(void);
}

namespace {

namespace gf = sh::gameflow;
namespace link = sh::gameflow::game;
namespace gs = sh::game_state;
namespace d2 = sh::draw2d;

std::int32_t g_title_save_count = 0;

// Game state names of gamedata/menu/title_menu.json: their numbers are the index.
constexpr std::string_view kGameStates[] = {
    "Init",          "KonamiLogo",         "KcetLogo",       "MovieIntroFadeIn", "AutoLoadSavegame",
    "MovieIntroAlternate", "MovieIntro",   "MainMenu",       "LoadSavegameScreen", "MovieOpening",
    "MainLoadScreen", "InGame",            "MapEvent",       "ExitMovie",        "InventoryScreen",
    "PaperMapScreen", "SaveScreen",        "DebugMoviePlayer", "OptionScreen",   "LoadStatusScreen",
    "LoadMapScreen",
};

constexpr int kStateOptionsMenu = 1;  // the in-game state slot doubles as "the fog has been seeded" on the title
constexpr std::int32_t kMainLoadScreen = 10;
constexpr int kNoDifficultyOverride = 99;
constexpr int kMapCafeExit = 10;  // map2_s00 in the registry's numbering (3 + 7 maps before it)
constexpr int kWhiteText = 7;
constexpr int kTextLength = 99;

// The story a map warp starts with (config/warp_story.json), loaded on the first warp.
const gf::WarpStory& warp_story() {
    static gf::WarpStory story;
    static std::uint32_t story_epoch = sh::mods::kNeverRead;
    if (sh::mods::data_stale(story_epoch)) {
        story = [] {
            char mods[1024];
            PcPaths_ExeFile(mods, sizeof(mods), "mods");
            std::string err;
            gf::WarpStory loaded = gf::load_config<gf::WarpStory>(link::gamedata_path(), mods, "warp_story.json", &err);
            if (!err.empty()) {
                SH_ERR("[WARP] config/warp_story.json: %s (warps start with every flag clear)", err.c_str());
            }
            return loaded;
        }();
    }
    return story;
}

// The flags of the chapters before the warp's map, set before the map loads: its init code reads them (map2_s00
// holds back its street monsters until the cafe exit is done).
void warp_story_apply(int map_id) {
    const char* map = sh::loader::map_name(map_id);
    if (map == nullptr) {
        return;
    }
    const std::vector<std::string> flags = warp_story().flags_for(map);
    for (const std::string& name : flags) {
        const int flag = link::flag_number(name.c_str());
        if (flag <= 0) {
            SH_ERR("[WARP] config/warp_story.json: unknown flag \"%s\"", name.c_str());
            continue;
        }
        gs::set_event_flag(flag, true);
    }
    if (!flags.empty()) {
        SH_LOG_INFO(MAP, "[WARP] %s: %d story flag(s) of the earlier chapters set", map, static_cast<int>(flags.size()));
    }
}

// The New Game boot of the title menu, also run by the map warp: the savegame wipe, the run hooks, the player and
// the first map load.
void new_game_boot(int map_id, std::int32_t base_difficulty, bool warp = false) {
    link::boot_savegame_initialize(map_id, base_difficulty);
    if (g_PcNewGameDifficulty != kNoDifficultyOverride) {
        gs::set_save_difficulty(g_PcNewGameDifficulty);
    }
    if (warp) {
        warp_story_apply(map_id);
    }
    // The game script and the mods start their run after the savegame wipe and before the map load, so their
    // per-map hooks see the run on the first area.
    link::new_game_hooks();
    link::boot_player_init();
    sh::game::session().system.process.process_flags = sh::game::process_flag::kNewGame;
    link::boot_map_load(link::saved_map());
    // Flags the console set (setending/setflag) were wiped with the savegame; they carry into the new game.
    link::console_apply_pending_flags();
}

void new_game_player_place() {
    // A new game started straight on map2_s00 (map = config): its point 0 is a parking spot outside the town, which
    // the original never uses because Harry always arrives through the cafe door, point 1.
    const gf::MapPointRecord point = link::map_point(link::saved_map() == kMapCafeExit ? 1 : 0);
    Chara_PositionSet(&point);
}

void title_screen_leave() {
    link::clear_title_area();
    link::screen_init(false);
}

int game_state_number(std::string_view name) {
    for (std::size_t i = 0; i < std::size(kGameStates); ++i) {
        if (kGameStates[i] == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int sound_number(std::string_view name) {
    static std::unordered_map<std::string, int> cache;
    const std::string key(name);
    auto it = cache.find(key);
    if (it == cache.end()) {
        const int number = name.empty() ? -1 : link::sound_number(key.c_str());
        if (number < 0 && !name.empty()) {
            SH_ERR("[GAMEFLOW] menu/title_menu.json: unknown sound \"%s\"", key.c_str());
        }
        it = cache.emplace(key, number).first;
    }
    return it->second;
}

gf::TitleMenuData load_data() {
    char mods[1024];
    PcPaths_ExeFile(mods, sizeof(mods), "mods");
    std::vector<std::string> warnings;
    std::string source;
    const sh::Result<std::string> text =
        gf::read_data_file(link::gamedata_path(), mods, "menu", "title_menu.json", &source, warnings);
    for (const std::string& w : warnings) {
        SH_WARN("[GAMEFLOW] %s", w.c_str());
    }
    if (!text) {
        SH_ERR("[GAMEFLOW] menu/title_menu.json: %s", text.error().c_str());
        return {};
    }
    sh::Result<gf::TitleMenuData> parsed = gf::TitleMenuData::parse(text.value());
    if (!parsed) {
        SH_ERR("[GAMEFLOW] menu/%s", parsed.error().c_str());
        return {};
    }
    for (const gf::TitleEntry& e : parsed.value().entries) {
        for (const std::string* state : {&e.next, &e.next_alt}) {
            if (!state->empty() && game_state_number(*state) < 0) {
                SH_ERR("[GAMEFLOW] menu/title_menu.json: unknown game state \"%s\"", state->c_str());
            }
        }
    }
    SH_LOG("[GAMEFLOW] menu/title_menu.json loaded%s%s", source.empty() ? "" : " from mod ", source.c_str());
    return parsed.value();
}

gf::TitleMenu& menu() {
    static gf::TitleMenu m;
    static std::uint32_t epoch = sh::mods::kNeverRead;
    if (sh::mods::data_stale(epoch)) {
        m.set_data(load_data());
    }
    return m;
}

void title_background_draw() {
    // The in-game state slot doubles as "the fog has been seeded" while the title is up.
    if (sh::game::session().system.flow.sys_state == sh::game::sys_state::kGameplay) {
        sh::game::session().set_sys_state_next(kStateOptionsMenu);
        Title_FogInit();
    }
    link::draw_picture(link::Picture::Title);
    MainMenu_FogUpdate();
}

sh::game::PadState& pad0() { return sh::game::session().game.pads[0]; }

class TitleMenuHost : public gf::TitleHost {
public:
    int idle_frames() const override {
        // No attract demo while the quick options are open over the title: the demo would close them.
        if (g_PcDebugMenuActive) {
            sh::game::session().system.flow.frame_counters[1] = 0;
        }
        return sh::game::session().system.flow.frame_counters[1];
    }
    int demo_count() const override { return gf::title_demo_count(); }
    void set_demo_count(int count) override { gf::title_demo_count() = static_cast<std::int8_t>(count); }

    bool skip_to_game() const override { return g_PcConfig.skipIntros >= 2; }
    bool attract_demos() const override { return g_PcConfig.attractDemos != 0; }
    bool autosave_alive() const override { return link::autosave_health() > 0; }
    void poll_saves() override { TitleMenu_PollSaves(); }
    int save_count() const override { return g_title_save_count; }

    gf::TitlePad pad() const override {
        const auto& binds = sh::game::session().game.settings.binds;
        gf::TitlePad p;
        // The debug menu over the title owns the pad (it reads it itself in its own tick).
        if (g_PcDebugMenuActive) {
            return p;
        }
        constexpr std::uint32_t kUp = 1u << 24, kDown = 1u << 26;
        const sh::game::PadState& pad = sh::game::session().game.pads[0];
        p.up = (pad.pulsed & kUp) != 0;
        p.down = (pad.pulsed & kDown) != 0;
        p.enter = (pad.clicked & binds.enter) != 0;
        p.cancel = (pad.clicked & binds.cancel) != 0;
        p.held_any = pad.held != 0;
        return p;
    }
    gf::TitleMouse mouse() const override {
        gf::TitleMouse m;
        // The quick options overlay over the title owns the mouse.
        if (g_PcDebugMenuActive) {
            return m;
        }
        m.in_view = sh::input::mouse_pointer().menu_position(&m.x, &m.y) != 0;
        m.moved = sh::input::mouse_pointer().moved() != 0;
        m.left = sh::input::mouse_pointer().left_clicked() != 0;
        m.right = sh::input::mouse_pointer().right_clicked() != 0;
        return m;
    }
    gf::ConfirmInput confirm_input() const override {
        const sh::ui::nm::Input in = sh::ui::nm::read_input();
        gf::ConfirmInput c;
        if (g_PcDebugMenuActive) {
            return c;
        }
        c.left = in.left != 0;
        c.right = in.right != 0;
        c.confirm = in.confirm != 0;
        c.cancel = in.cancel != 0;
        c.mouse_moved = in.pointer_moved != 0;
        c.mouse_left = in.click != 0;
        c.mouse_right = in.right_click != 0;
        return c;
    }
    void press_enter() override { pad0().clicked |= sh::game::session().game.settings.binds.enter; }
    void press_cancel() override { pad0().clicked |= sh::game::session().game.settings.binds.cancel; }
    void release_enter() override { pad0().clicked &= ~static_cast<std::uint32_t>(sh::game::session().game.settings.binds.enter); }
    void consume_pad() override {
        pad0().pulsed = 0;
        pad0().clicked = 0;
    }

    void open_title_screen() override {
        sh::game::session().game.clear_color.r = 0;
        sh::game::session().game.clear_color.g = 0;
        sh::game::session().game.clear_color.b = 0;
        link::clear_interlaced_screen();
        link::screen_init(true);
        link::set_interval_vblanks(1);
        gs::screen_fade_start(true, true, false);
        gs::set_screen_fade_timestep(2 * 4096);
    }
    void demo_startup() override { link::boot_game_startup(); }
    void reset_file_queue() override { link::reset_file_queue(); }
    void play_sound(std::string_view name) override {
        const int number = sound_number(name);
        if (number >= 0) {
            gs::sound_command(number);
        }
    }
    void fade_in() override { gs::screen_fade_start(true, true, false); }
    void fade_out() override { gs::screen_fade_start(true, false, false); }
    void fade_reset() override { link::screen_fade_reset(); }
    bool fade_finished() override { return gs::screen_fade_finished(); }
    void continue_game() override {
        if (link::autosave_health() > 0) {
            sh::game::session().game.save = sh::game::session().game.autosave;
        } else {
            gs::load_save_screen_files();
        }
        link::boot_player_init();
        sh::game::session().system.process.process_flags = sh::game::process_flag::kContinue;
        link::boot_map_load(link::saved_map());
    }
    void load_save_screen() override { gs::load_save_screen_files(); }
    void load_options_screen() override { link::load_options_screen(); }
    int difficulty_count() const override { return link::difficulty_count(); }
    const char* difficulty_label(int index) const override { return link::difficulty_label(index); }
    int difficulty_default() const override { return link::difficulty_default(); }
    void start_new_game(int difficulty) override {
        // A difficulty's own start map (difficulties.json) beats the configured map; without either the game starts
        // on the first map, as the original does for every difficulty.
        const char* difficulty_map = link::difficulty_start_map(difficulty);
        int map_id = sh::loader::find_map(difficulty_map != nullptr ? difficulty_map : g_PcConfig.mapName);
        if (map_id < 0) {
            map_id = 0;
        }
        new_game_boot(map_id, link::difficulty_base(difficulty));
    }
    void load_movie_stream() override { link::load_movie_stream(); }
    int file_queue_length() override { return link::file_queue_length(); }
    void quit() override {
        SH_DBG("[MENU] Exit selected — closing game");
        link::flush_debug_log();
        exit(0);
    }
    void finish_loading() override {
        link::screen_refresh();
        link::wait_file_queue();
    }
    void place_new_game_player() override { new_game_player_place(); }
    void change_game_state(std::string_view name) override {
        const int state = game_state_number(name);
        if (state < 0) {
            SH_ERR("[GAMEFLOW] title menu: no game state \"%.*s\"", static_cast<int>(name.size()), name.data());
            return;
        }
        sh::game::session().set_game_state_next(state);
    }
    void clear_background() override {
        sh::game::session().game.clear_color.r = 0;
        sh::game::session().game.clear_color.g = 0;
        sh::game::session().game.clear_color.b = 0;
    }

    void draw_background() override { title_background_draw(); }
    void show_cursor() override { sh::input::mouse_pointer().show(); }
    void leave_title_screen() override { title_screen_leave(); }
    bool text_translated(const char* text) const override { return sh::i18n::menu_text(text) != text; }
    int text_width(const char* text) const override { return sh::i18n::text_width(sh::i18n::menu_text(text)); }
    void text_position(int x, int y) override { sh::text::game_text().set_position(x, y); }
    void text_white() override { sh::text::game_text().set_color(kWhiteText); }
    void text(const char* text) override { sh::text::game_text().draw(const_cast<char*>(text), kTextLength); }
};

int g_warp_map = -1;

}  // namespace

// The saves the title menu offers come from the engine's save catalog.
extern "C" void TitleMenu_PollSaves(void) {
    link::poll_saves();
    g_title_save_count = link::save_catalog_count();
}

// The console's `options` on the title: the title picks its OPTION entry itself.
extern "C" int TitleMenu_PickOptions(void) {
    return menu().request_pick(gf::TitleEntryKind::Options) ? 1 : 0;
}

extern "C" void MainMenu_SelectedOptionIdxReset(void) {
    menu().select_continue();
}

extern "C" void GameState_MainMenu_Update(void) {
    TitleMenuHost host;
    menu().frame(host);
    // The debug menu (F10) over the title: its input and draw run after the title's own frame.
    if (g_PcDebugMenuActive) {
        Pc_DebugMenu_Update();
    }
}

// Map warp (quick options Go to map, console MAP in game): a New Game on the picked map, started from the title or
// from play. The request only records the map; the main frame runs it at its warm reset point, between frames, where
// nothing of the old session is mid-update.
extern "C" int Pc_MapWarp_Request(int mapId) {
    if (mapId < 0 || mapId >= sh::loader::map_count() || sh::loader::map_name(mapId) == nullptr) {
        return 0;
    }
    // Play and the title only: the load screens, the save screens and the movies own the session state.
    const std::int32_t state = sh::game::session().game.game_state;
    if (state != sh::game::game_state::kInGame && state != sh::game::game_state::kMainMenu) {
        return 0;
    }
    if (sh::game::session().sys_flag(sh::game::sys_flag::kDemo)) {
        return 0;
    }
    g_warp_map = mapId;
    return 1;
}

extern "C" int Pc_MapWarp_Due(void) {
    return g_warp_map >= 0;
}

extern "C" void Pc_MapWarp_Run(void) {
    const int map_id = g_warp_map;
    g_warp_map = -1;
    if (map_id < 0) {
        return;
    }
    // From play the run keeps its difficulty; from the title it is the picker's default, as for New Game.
    const bool from_title = sh::game::session().game.game_state == sh::game::game_state::kMainMenu;
    const std::int32_t difficulty =
        from_title ? link::difficulty_base(link::difficulty_default()) : static_cast<std::int32_t>(gs::save_difficulty());
    SH_LOG_INFO(MAP, "[WARP] to %s (%s) from the %s", sh::loader::map_name(map_id), sh::loader::map_description(map_id),
                from_title ? "title" : "game");

    // The exit to the title first: it stops the audio, the file queue, a demo and the pause menu and clears the play
    // state, the same reset the pause menu's Exit runs.
    link::warm_boot();
    new_game_boot(map_id, difficulty, true);
    link::screen_refresh();
    link::wait_file_queue();
    new_game_player_place();
    title_screen_leave();
    sh::game::session().set_game_state_next(kMainLoadScreen);
    const sh::game::Player& player = sh::game::player();
    SH_LOG_INFO(MAP, "[WARP] %s: player placed at (%.2f, %.2f, %.2f)", sh::loader::map_name(map_id),
                player.get(sh::game::PlayerVar::PositionX) / 4096.0, player.get(sh::game::PlayerVar::PositionY) / 4096.0,
                player.get(sh::game::PlayerVar::PositionZ) / 4096.0);
}

// Whether the current map can load again in place now: in play (not a demo), outside events, cutscenes and menus,
// where a save point could save too.
extern "C" int Pc_MapReload_Possible(void) {
    return sh::game::session().game.game_state == sh::game::game_state::kInGame &&
           gs::sys_state() == gs::SysState::Gameplay && !sh::game::session().sys_flag(sh::game::sys_flag::kDemo);
}

// `mods reload` in play: the running save takes Harry's place and health as a save point would, the session goes
// through the exit to the title (audio, the file queue, the play state), `between` runs while nothing of play is alive
// (the data read again), and the same save loads the same map again as the load screen's Continue does: inventory,
// flags and the map's state come back from the save, the map, its scripts and characters from the data.
extern "C" void Pc_MapReload_Run(void (*between)(void)) {
    SysWork_SavegameUpdatePlayer();
    auto& game = sh::game::session().game;
    const sh::game::SaveRecord kept = game.save;
    const int map = link::saved_map();
    SH_LOG_INFO(MAP, "[RELOAD] %s loads again in place", sh::loader::map_name(map));
    link::warm_boot();
    if (between != nullptr) {
        between();
    }
    game.save = kept;
    game.autosave = kept;
    link::boot_player_init();
    sh::game::session().system.process.process_flags = sh::game::process_flag::kLoadSave;
    link::boot_map_load(link::saved_map());
    link::screen_refresh();
    link::wait_file_queue();
    title_screen_leave();
    sh::game::session().set_game_state_next(kMainLoadScreen);
}

std::int8_t& sh::gameflow::title_demo_count() {
    static std::int8_t count = 0;
    return count;
}
