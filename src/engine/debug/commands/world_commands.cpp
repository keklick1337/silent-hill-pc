// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Console commands that move the game about: map, pos, tp, event, fmv, difficulty and the save and load
// screens. `event` and `fmv` only queue: the frame loop starts them when the game can take them.

#include "engine/config/pc_config.h"
#include "engine/debug/command_services.h"
#include "engine/debug/commands/command_groups.h"
#include "engine/debug/commands/command_kit.h"
#include "engine/debug/commands/game_switches.h"
#include "engine/debug/commands/spawn_roster.h"
#include "engine/debug/dbg_trace.h"
#include "engine/debug/overlay/debug_overlay.h"
#include "engine/game_state/game_state.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/log/sh_log.h"
#include "features/quicksave/quick_save.h"
#include "media/fmv/movie_player.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace sh::debug {

namespace {

using namespace sh::debug::commands;
namespace gs = sh::game_state;

// ---- map ----

void complete_maps(std::string_view, std::vector<Candidate>& out) {
    static std::vector<Candidate> cache;  // the map table does not change while the game runs
    if (cache.empty()) {
        for (int map = 0; map < gs::map_count(); ++map) {
            const char* name = gs::map_name(map);
            if (name == nullptr || name[0] == '\0') continue;
            const char* about = gs::map_description(map);
            cache.push_back({name, about != nullptr ? about : ""});
        }
    }
    out.insert(out.end(), cache.begin(), cache.end());
}

void say_where(Context& ctx) {
    const char* overlay = gs::map_overlay_name();
    say(ctx, "map %s - %s", gs::map_name(gs::current_map()), gs::map_description(gs::current_map()));
    say(ctx, " overlay %s, room %d, game state %s, system state %s", overlay != nullptr ? overlay : "built-in/none",
        gs::save_room(), DbgTrace_GameStateName(gs::game_state_raw()), DbgTrace_SysStateName(gs::sys_state_raw()));
}

Result<void> run_map(Context& ctx, const Args& args) {
    const std::string name = lower(args.joined(0)).substr(0, 31);
    if (name.empty()) {
        say_where(ctx);
        LineWrapper names(ctx, 200, 1, " ");
        for (int map = 0; map < gs::map_count(); ++map) {
            if (const char* known = gs::map_name(map)) names.add(known);
        }
        names.flush();
        return done();
    }

    const int map = gs::map_find(name.c_str());
    if (map < 0) {
        say(ctx, "Unknown map: %s", name.c_str());
        return done();
    }
    const char* about = gs::map_description(map);
    const bool described = about != nullptr && about[0] != '\0';

    // In play the warp is a New Game on the map, at once; on the title the map waits for the next New Game.
    if (gs::game_state() == gs::GameState::InGame) {
        if (Pc_MapWarp_Request(map)) {
            say(ctx, "going to %s%s%s", name.c_str(), described ? " - " : "", about != nullptr ? about : "");
        } else {
            say(ctx, "map: cannot warp now");
        }
        return done();
    }
    std::snprintf(g_PcConfig.mapName, sizeof(g_PcConfig.mapName), "%s", name.c_str());
    if (described) {
        say(ctx, "map config set to %s - %s (loads on New Game)", name.c_str(), about);
    } else {
        say(ctx, "map config set to %s (loads on New Game)", name.c_str());
    }
    return done();
}

// ---- pos / tp ----

int degrees_of(int angle_q12) { return ((angle_q12 & 0xFFF) * 360) >> 12; }

void say_position(Context& ctx) {
    const sh::entities::Vec3 at = gs::chara_position(0);
    say(ctx, "Harry %.2f %.2f %.2f  yaw %d deg  map %s room %d", at.x / 4096.0f, at.y / 4096.0f, at.z / 4096.0f,
        degrees_of(gs::chara_rotation(0).y), gs::map_name(gs::current_map()), gs::save_room());
    say(ctx, "raw   %d %d %d (Q12)", static_cast<int>(at.x), static_cast<int>(at.y), static_cast<int>(at.z));
}

Result<void> run_tp(Context& ctx, const Args& args) {
    const std::vector<std::string> words = spaced_words(args);
    std::size_t numbers = words.size() < 4 ? words.size() : 4;
    if (numbers == 4) {
        // The fourth number is the facing in degrees, to reproduce a reported view.
        const int yaw = static_cast<int>(real(words[3]) / 360.0 * 4096.0) & 0xFFF;
        gs::set_chara_rotation_y(0, static_cast<std::int16_t>(yaw));
        numbers = 3;
    }
    if (numbers != 2 && numbers != 3) {
        say(ctx, "tp <x> <z> | tp <x> <y> <z> [yaw]  (world units, as 'pos' prints them; yaw in degrees)");
        return done();
    }
    const bool with_height = numbers == 3;
    const std::int32_t x = static_cast<std::int32_t>(real(words[0]) * 4096.0);
    const std::int32_t z = static_cast<std::int32_t>(real(words[with_height ? 2 : 1]) * 4096.0);
    const std::int32_t y = with_height ? static_cast<std::int32_t>(real(words[1]) * 4096.0) : 0;
    gs::place_player(x, z, with_height ? &y : nullptr);

    const sh::entities::Vec3 now = gs::chara_position(0);
    SH_LOG_INFO(CONSOLE, "tp Harry to %.2f %.2f %.2f", now.x / 4096.0f, now.y / 4096.0f, now.z / 4096.0f);
    say_position(ctx);
    return done();
}

// ---- event ----
//
// Starts one entry of the map's event table the way the game starts it when its trigger passes: it becomes the
// current event and its system state the next one, so the same tick runs it. The trigger area and the flags
// are not tested (a test reaches the handler without walking there). The request waits until the game is in
// play with the map's events loaded and no fade running: the replay harness asks before the map is there.
class EventQueue {
public:
    void ask_index(int index, bool move_player) {
        index_ = index;
        slot_ = -1;
        move_player_ = move_player;
        waiting_ = true;
    }
    // The first event that calls handler slot `slot`; found when the request fires, as the table may not be
    // loaded yet.
    void ask_slot(int slot, bool move_player) {
        index_ = -1;
        slot_ = slot;
        move_player_ = move_player;
        waiting_ = true;
    }
    bool waiting() const { return waiting_; }

    void tick() {
        if (!waiting_ || !game_ready()) return;
        waiting_ = false;
        const int count = gs::map_event_count();
        int index = index_;
        if (slot_ >= 0) {
            index = -1;
            for (int i = 0; i < count && index < 0; ++i) {
                const gs::MapEvent event = gs::map_event(i);
                if (event.sys_state == static_cast<int>(gs::SysState::EventCallback) && event.param == slot_) index = i;
            }
            if (index < 0) {
                tell("event: no event of this map calls handler slot %d", slot_);
                std::fprintf(stderr, "[EVENT] no event calls handler slot %d\n", slot_);
                return;
            }
        }
        if (index >= count) {
            tell("event: %d is past the last event (%d)", index, count - 1);
            return;
        }
        start(index);
    }

private:
    static bool game_ready() {
        return gs::game_state() == gs::GameState::InGame && gs::sys_state() == gs::SysState::Gameplay &&
               gs::map_events_loaded() && gs::screen_fade_idle();
    }

    // The queue speaks from the frame loop as well as from the command, so its lines go to the console itself.
    template <typename... Values>
    static void tell(const char* format, Values... values) {
        char line[kLineLimit + 1];
        std::snprintf(line, sizeof(line), format, values...);
        console_print(line);
    }

    void start(int index) const {
        const gs::MapEvent event = gs::map_event(index);
        if (move_player_) gs::map_event_place_player(index);
        // On stderr, not in the log: the replay harness reads the tick from the run's output.
        std::fprintf(stderr, "[EVENT] fired %d: sysState %d param %d poi %d at tick %d\n", index, event.sys_state,
                     event.param, event.point, gs::tick_count());
        tell("event %d started (system state %d, param %d)", index, event.sys_state, event.param);
        gs::map_event_start(index);
    }

    bool waiting_ = false;
    int index_ = -1;
    int slot_ = -1;
    bool move_player_ = false;
};

EventQueue& event_queue() {
    static EventQueue queue;
    return queue;
}

void list_events(Context& ctx) {
    const int count = gs::map_event_count();
    if (count == 0) {
        say(ctx, "event: no map event table loaded");
        return;
    }
    for (int i = 0; i < count; ++i) {
        const gs::MapEvent event = gs::map_event(i);
        say(ctx, "%3d trig %d act %d sys %2d param %3d poi %3d req %4d dis %4d%s", i, event.trigger_type,
            event.activation_type, event.sys_state, event.param, event.point, event.required_flag,
            event.disabled_flag,
            event.sys_state == static_cast<int>(gs::SysState::EventCallback) ? "  (handler slot = param)" : "");
    }
    say(ctx, "%d map event(s); 'event <idx> [at]' starts one, 'event slot <n> [at]' the first handler of slot n",
        count);
}

bool starts_with_digit(const std::string& word) {
    return !word.empty() && std::isdigit(static_cast<unsigned char>(word[0])) != 0;
}

Result<void> run_event(Context& ctx, const Args& args) {
    std::vector<std::string> words = spaced_words(args);
    for (std::string& word : words) word = lower(word).substr(0, 15);
    const auto word = [&words](std::size_t index) { return index < words.size() ? words[index] : std::string(); };

    if (words.empty()) {
        list_events(ctx);
        return done();
    }
    std::size_t next = 1;
    const bool by_slot = word(0) == "slot";
    if (by_slot) {
        if (!starts_with_digit(word(1))) {
            say(ctx, "event slot <n> [at]");
            return done();
        }
        next = 2;
    } else if (!starts_with_digit(word(0))) {
        say(ctx, "event | event <idx> [at] | event slot <n> [at]");
        return done();
    }
    const int number = whole(word(next - 1));
    const bool move_player = word(next) == "at";
    if (by_slot) {
        event_queue().ask_slot(number, move_player);
    } else {
        event_queue().ask_index(number, move_player);
    }
    tick_command_services();
    if (event_queue().waiting()) say(ctx, "event: queued until gameplay");
    return done();
}

// ---- fmv ----

// The movie waits for the fade to black, like the game's own movie transitions.
int s_queued_movie_file = -1;

void complete_movies(std::string_view, std::vector<Candidate>& out) {
    static std::vector<Candidate> cache;
    const std::vector<sh::fmv::Movie>& movies = sh::fmv::movie_list().movies();
    if (!movies.empty() && cache.empty()) {
        for (std::size_t i = 0; i < movies.size(); ++i) cache.push_back({movies[i].name, std::to_string(i + 1)});
        for (int i = 1; i <= 2; ++i) cache.push_back({"intro" + std::to_string(i), "alias"});
        for (int i = 1; i <= 5; ++i) cache.push_back({"end" + std::to_string(i), "alias"});
    }
    out.insert(out.end(), cache.begin(), cache.end());
}

bool all_digits(const std::string& text) {
    for (const char c : text) {
        if (std::isdigit(static_cast<unsigned char>(c)) == 0) return false;
    }
    return true;
}

// The n-th movie (1-based, disc order) whose file name starts with `lead`; -1 when there are fewer.
int nth_movie_starting(char lead, int n) {
    const std::vector<sh::fmv::Movie>& movies = sh::fmv::movie_list().movies();
    int seen = 0;
    for (std::size_t i = 0; i < movies.size(); ++i) {
        if (movies[i].name[0] == lead && ++seen == n) return static_cast<int>(i);
    }
    return -1;
}

Result<void> run_fmv(Context& ctx, const Args& args) {
    const std::string movie = upper(args.joined(0));
    const std::vector<sh::fmv::Movie>& movies = sh::fmv::movie_list().movies();
    const int count = static_cast<int>(movies.size());

    if (movie.empty()) {
        char status[96];
        DbgTrace_FmvStatus(status, sizeof(status));
        say(ctx, "fmv: %s", status);
        LineWrapper list(ctx, 60, 1, " ");
        for (int i = 0; i < count; ++i) {
            list.add((std::to_string(i + 1) + "=" + movies[static_cast<std::size_t>(i)].name).substr(0, 23));
        }
        list.flush();
        say(ctx, "also: fmv <number>, intro1-2, end1-5");
        return done();
    }

    // A number is the 1-based place in that list; introN / endN are the intros (C...) and endings (Z...).
    int pick = all_digits(movie) ? whole(movie) - 1 : -1;
    if (pick < 0 && (movie.rfind("INTRO", 0) == 0 || movie.rfind("END", 0) == 0)) {
        const bool intro = movie[0] == 'I';
        pick = nth_movie_starting(intro ? 'C' : 'Z', whole(movie.substr(intro ? 5 : 3)));
        if (pick < 0) {
            say(ctx, "No such %s", intro ? "intro" : "ending");
            return done();
        }
    }
    for (int i = 0; pick < 0 && i < count; ++i) {
        if (movie == movies[static_cast<std::size_t>(i)].name) pick = i;
    }
    if (pick < 0 || pick >= count) {
        say(ctx, "Unknown FMV: %s (try 'fmv' to list)", movie.c_str());
        return done();
    }

    const sh::fmv::Movie& picked = movies[static_cast<std::size_t>(pick)];
    say(ctx, "Playing %s...", picked.name.c_str());
    s_queued_movie_file = sh::loader::disc_files().find(picked.file);
    gs::screen_fade_start(true, false, false);
    return done();
}

// ---- difficulty ----

// Tests and modders check difficulty-dependent data (spawn_overrides.json) without a new game: set it, then
// load the map again.
Result<void> run_difficulty(Context& ctx, const Args& args) {
    const std::string& level = args.str(0);
    if (level.empty()) {
        say(ctx, "usage: difficulty easy|normal|hard (now %d)", gs::save_difficulty());
        return done();
    }
    gs::set_save_difficulty(level == "easy" ? -1 : level == "hard" ? 1 : 0);
    say(ctx, "difficulty %s; reload the map (map <name>) to apply its data", upper(level).c_str());
    return done();
}

}  // namespace

void tick_command_services() {
    spawn_roster().preview_tick();
    event_queue().tick();
}

void play_queued_movie() {
    if (s_queued_movie_file < 0 || !gs::screen_fade_finished()) return;
    const int file = s_queued_movie_file;
    s_queued_movie_file = -1;
    sh::fmv::play_movie(file, 0);
    g_PcFmvClockDiscard = 1;
    gs::screen_fade_start(true, true, false);
}

namespace commands {

void register_world_commands(Registry& registry) {
    add(registry, "map", "game", "warp to a map, or show the current map and the list",
        {arg("name", ArgType::String, true, "map name", {}, complete_maps)}, run_map, {},
        "In play the game warps at once (a new game on that map); on the title it sets the map the next New Game "
        "starts on.");

    add(registry, "pos", "debug", "Harry's position, heading, map and room", {}, [](Context& ctx, const Args&) {
        say_position(ctx);
        return done();
    });
    add(registry, "tp", "player", "teleport Harry (world units, as 'pos' prints them)",
        {arg("x", ArgType::Float, false, "X"), arg("y", ArgType::Float, false, "Y, or Z when only two numbers are given"),
         arg("z", ArgType::Float, true, "Z; with it the second number is the height"),
         arg("yaw", ArgType::Float, true, "facing in degrees (after x y z)")},
        run_tp, {}, "tp <x> <z>      keeps the current height\ntp <x> <y> <z> [yaw]");

    add(registry, "event", "debug", "list or start the current map's events",
        {arg("what", ArgType::String, true, "event index, or 'slot'", {"slot"}),
         arg("n", ArgType::String, true, "slot number (after 'slot'), or 'at'", {"at"}),
         arg("at", ArgType::String, true, "'at' also moves Harry onto the event's map point", {"at"})},
        run_event, {},
        "event                  list the map's events\n"
        "event <idx> [at]       start one (its trigger area and flags are not checked)\n"
        "event slot <n> [at]    start the first event of handler slot n");

    add(registry, "fmv", "system", "list or play a movie",
        {arg("movie", ArgType::String, true, "name, list number, introN or endN", {}, complete_movies)}, run_fmv);

    add(registry, "difficulty", "game", "set the difficulty (reload the map to apply it)",
        {arg("level", ArgType::Enum, true, "difficulty", {"easy", "normal", "hard"})}, run_difficulty);

    // Automation needs the save and load screens without a notebook or the quick keys (offscreen runs have no
    // keyboard); like the other cheats they ignore save_anywhere.
    add(registry, "savescreen", "game", "open the save screen here (debug)", {},
        [](Context& ctx, const Args&) -> Result<void> {
            if (gs::game_state() != gs::GameState::InGame || gs::sys_state() != gs::SysState::Gameplay) {
                ctx.out.error("only in gameplay");
                SH_LOG_INFO(SAVE, "savescreen: not in gameplay (game state %d, sys state %d)",
                            static_cast<int>(gs::game_state()), static_cast<int>(gs::sys_state()));
                return Result<void>::fail("not in gameplay");
            }
            SH_LOG_INFO(SAVE, "savescreen: opening the save screen");
            gs::open_save_screen();
            return done();
        });
    add(registry, "loadscreen", "game", "open the load screen (debug)", {},
        [](Context& ctx, const Args&) -> Result<void> {
            if (gs::game_state() != gs::GameState::InGame) {
                ctx.out.error("only in game");
                return Result<void>::fail("not in game");
            }
            SH_LOG_INFO(SAVE, "loadscreen: opening the load screen");
            gs::open_load_screen();
            return done();
        });
    // The quick keys of quick_save_instant for runs without a keyboard: the same checks, the same quick slot.
    add(registry, "quicksave", "game", "save the quick slot at once, as the quick save key (quick_save_instant = 1)",
        {}, [](Context& ctx, const Args&) -> Result<void> {
            if (!sh::quicksave::save_now(true)) {
                ctx.out.error("not saved (quick_save_instant off, or saving is not allowed here)");
                return Result<void>::fail("not saved");
            }
            return done();
        });
    add(registry, "quickload", "game", "load the quick slot at once, as the quick load key (quick_save_instant = 1)",
        {}, [](Context& ctx, const Args&) -> Result<void> {
            if (!sh::quicksave::load_now(true)) {
                ctx.out.error("not loaded (quick_save_instant off, no quick save, or not in gameplay)");
                return Result<void>::fail("not loaded");
            }
            return done();
        });
}

}  // namespace commands
}  // namespace sh::debug
