// SPDX-License-Identifier: GPL-3.0-or-later
// The movie game states on the engine (src/engine/screens/movie_states.h): the game's input, fades, file table
// and movie player for the screens gamedata/menu/movies.json describes. The GameState_Movie*_Update entries are
// the game state table's; open_main plays a movie for the event code.

#include <cstdint>
#include <optional>
#include <string>

#include "audio/voice/voice_line_player.h"
#include "engine/app/pc_paths.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/state_names.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/log/sh_log.h"
#include "engine/manifest/map_manifest.h"
#include "engine/screens/movie_states.h"
#include "engine/screens/screens_view.h"
#include "engine/text/game_text.h"
#include "media/fmv/movie_player.h"

extern "C" const char* PcPort_GetGameDataPath(void);
// The movie's whole runtime lands in one frame of the frame clock; the clock drops that frame's delta.
extern "C" int g_PcFmvClockDiscard;

namespace {

namespace sc = sh::screens;
namespace gs = sh::game_state;

sh::game::Session& game() { return sh::game::session(); }
const sh::game::PadState& pad() { return sh::game::session().game.pads[0]; }

std::optional<sc::MovieStates> s_spec;
std::optional<sc::MovieScreens> s_screens;

sc::MovieScreens& screens()
{
    if (!s_screens) {
        char mods[1024];
        PcPaths_ExeFile(mods, sizeof(mods), "mods");
        std::string source;
        sh::Result<sc::MovieStates> loaded = sc::load_movie_states(PcPort_GetGameDataPath(), mods, &source);
        if (loaded) {
            s_spec.emplace(std::move(loaded.value()));
            SH_LOG("[MOVIE] movie states from %s", source.c_str());
        } else {
            SH_ERR("[MOVIE] %s - movies are not played", loaded.error().c_str());
            s_spec.emplace();
        }
        s_screens.emplace(*s_spec);
    }
    return *s_screens;
}

// A state by its data name. The two states the game's table has no screen for keep their numbers, so the unused
// debug player's cancel still names one.
std::int32_t state_by_name(const std::string& name)
{
    const std::int32_t state = sh::gameflow::game_state_by_name(name);
    if (state >= 0) return state;
    if (name == "Unk15") return 21;
    if (name == "Unk16") return 22;
    return -1;
}

class MovieHostImpl final : public sc::MovieHost {
public:
    std::int32_t step(int level) override { return game().game.game_steps[level]; }
    void set_step(int level, std::int32_t value) override { game().game.game_steps[level] = value; }
    sc::Fade fade() override
    {
        const std::int32_t v = sc::fade_status();
        if (v < 0 || v > 15) {
            return {sc::FadeState::Other, false};
        }
        return {static_cast<sc::FadeState>(v & 7), (v & 8) != 0};
    }
    void set_fade(sc::Fade f) override { sc::set_fade_status(static_cast<std::int32_t>(f.state) | (f.white ? 8 : 0)); }
    void set_fade_step(std::int32_t step_q12) override { sc::set_fade_timestep(step_q12); }
    void wait_vblanks(std::int32_t count) override { sc::wait_vblanks(count); }
    void set_next_state(const std::string& name) override
    {
        const std::int32_t state = state_by_name(name);
        if (state < 0) {
            SH_ERR("[MOVIE] movies.json: unknown game state \"%s\"", name.c_str());
            return;
        }
        game().set_game_state_next(state);
    }

    bool any_button_held() override { return pad().held != 0; }
    bool clicked_enter() override { return (pad().clicked & game().game.settings.binds.enter) != 0; }
    bool clicked_cancel() override { return (pad().clicked & game().game.settings.binds.cancel) != 0; }
    bool pulsed_left() override { return (pad().pulsed & gs::pad::kStickLeft) != 0; }
    bool pulsed_right() override { return (pad().pulsed & gs::pad::kStickRight) != 0; }
    std::int32_t frame_counter() override { return game().system.flow.frame_counters[0]; }
    bool extra_option(std::int32_t bit) override { return (game().game.settings.extra_options & (1 << bit)) != 0; }

    void start_title_loads() override { sc::start_title_loads(); }
    void wait_loads() override { sc::wait_for_file_queue(); }
    void draw_card_message() override { sc::draw_screen_image(sc::ScreenImage::CardMessage); }
    void set_debug_text_position(std::int32_t x, std::int32_t y) override
    {
        sh::text::game_text().set_debug_position(x, y);
    }

    std::string manifest_string(const std::string& key) override
    {
        const char* v = MapManifest_String(key.c_str());
        return v != nullptr ? v : "";
    }
    std::int32_t manifest_int(const std::string& key) override { return MapManifest_Int(key.c_str()); }

    std::int32_t find_movie(const std::string& name) override
    {
        // The data names "DIR/NAME" exactly as the table spells it.
        const sh::loader::DiscFiles& files = sh::loader::disc_files();
        const int idx = files.find(name);
        return idx != sh::loader::DiscFiles::kNone && name == files.path(idx) ? idx : -1;
    }
    std::string movie_name(std::int32_t id) override { return sh::loader::disc_files().path(id); }
    void warn(const std::string& message) override { SH_LOG("[MOVIE] %s", message.c_str()); }

    void silence_world_audio() override
    {
        sh::audio::voice_lines().stop();
        sc::stop_all_sound_effects();
    }
    void play_movie(std::int32_t id, std::int32_t frames) override
    {
        if (id < 0 || id >= sh::loader::disc_files().count()) {
            SH_ERR("[MOVIE] no movie file %d", static_cast<int>(id));
            return;
        }
        sh::fmv::play_movie(id, static_cast<std::int16_t>(frames));
        SH_DBG("[SH] open_main: the movie returned");
    }
    void after_movie() override { g_PcFmvClockDiscard = 1; }
};

MovieHostImpl s_host;

}  // namespace

extern "C" {

void GameState_MovieIntroFadeIn_Update(void) { screens().intro_fade_in(s_host); }
void GameState_MovieIntro_Update(void) { screens().intro(s_host); }
void GameState_MovieOpening_Update(void) { screens().opening(s_host); }
void GameState_ExitMovie_Update(void) { screens().exit_movie(s_host); }
void GameState_DebugMoviePlayer_Update(void) { screens().debug_player(s_host); }
void GameState_MovieIntroAlternate_Update(void) { screens().intro_alternate(s_host); }

void open_main(std::int32_t file_idx, std::int16_t num_frames)
{
    SH_DBG("[SH] open_main: playing FMV (file=%d frames=%d)", static_cast<int>(file_idx), static_cast<int>(num_frames));
    screens().play(s_host, file_idx, num_frames);
}

}  // extern "C"
