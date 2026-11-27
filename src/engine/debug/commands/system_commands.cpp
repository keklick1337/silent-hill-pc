// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Console commands of the program itself: quit, restart, version, the key reference, the menus, key binds, the window,
// the log, the frame counter, the console's own scale, the menu style and the voice volume.

#include "audio/voice/voice_line_player.h"
#include "engine/audio/engine.h"
#include "engine/audio/file_player.h"
#include "engine/audio/sound_registry.h"
#include "engine/config/pc_config.h"
#include "engine/crash/crash_handler.h"
#include "engine/debug/commands/command_groups.h"
#include "engine/debug/commands/command_kit.h"
#include "engine/debug/commands/game_switches.h"
#include "engine/debug/console_host.h"
#include "engine/debug/dbg_trace.h"
#include "engine/entities/game/radio_state.h"
#include "engine/log/sh_log.h"
#include "engine/menu/menu_model.h"
#include "engine/mods/mods_admin.h"
#include "engine/mods/registry.h"
#include "engine/platform/window.h"
#include "engine/version/engine_version.h"
#include "input/bind/console_binds.h"
#include "ui/menu/pc_debug_menu.h"
#include "ui/screens/title_style.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace sh::debug::commands {

namespace {

constexpr const char* kKeysPage1[] = {
    "Debug keys (page 1/2) - what is left on the keyboard:",
    " F10     DEBUG OPTIONS menu (needs Options > Interface > Debug Options)",
    " F11     fullscreen on/off (also Alt+Enter)",
    " , .     keyframe scrub while the viewer is on (hold = faster)",
    " ~       console open/close (game pauses; PgUp/PgDn scroll)",
    " TAB     with the console open: hide the panel, keep the cursor",
    "No function key does a debug action. Console: ff (fast forward),",
    "shot, vramdump, pgxp, postfx, tonemap, flmode; cheats and tools",
    "are in DEBUG OPTIONS (god, noclip, spawn, collision view).",
    "type DEBUG 2 for the camera keys",
};

constexpr const char* kKeysPage2[] = {
    "Debug keys (page 2/2) - camera:",
    " Num *        free camera on/off (also DEBUG OPTIONS > Camera)",
    " Free camera: mouse look, W/A/S/D move, Space/C up/down,",
    "              Shift fast, Ctrl slow. Fog starts off so the",
    "              world is visible outdoors; Num . toggles it.",
    " Num .        log Harry position (+ fog toggle in free cam)",
    " Num 3        rescue teleport after falling through a floor",
};

const char* log_file_or_hint() {
    return ShLog_FilePath()[0] != '\0' ? ShLog_FilePath() : "off (config log_file = 1)";
}

void list_log(Context& ctx) {
    say(ctx, "log levels (off error warn info debug trace); on screen up to %s:",
        ShLog_LevelName(ShLog_ConsoleLevel()));
    for (int category = 0; category < SH_LOGCAT_COUNT; ++category) {
        say(ctx, " %-8s %-5s  %s", ShLog_CategoryName(category), ShLog_LevelName(ShLog_CategoryLevel(category)),
            ShLog_CategoryHelp(category));
    }
    say(ctx, "file: %s", log_file_or_hint());
}

// A word of the log command: lower case, as long as a category or level name can be.
std::string log_word(const std::vector<std::string>& words, std::size_t index) {
    return index < words.size() ? lower(words[index]).substr(0, 23) : std::string();
}

Result<void> run_log(Context& ctx, const Args& args) {
    const std::vector<std::string> words = spaced_words(args);
    const std::string what = log_word(words, 0);
    const std::string level_word = log_word(words, 1);

    if (what.empty() || what == "list") {
        list_log(ctx);
        return done();
    }
    if (what == "file") {
        say(ctx, "log file: %s", log_file_or_hint());
        return done();
    }
    if (what == "screen") {
        const int level = ShLog_ParseLevel(level_word.c_str());
        if (level < 0) {
            say(ctx, "log screen <level>: lines shown here, now %s", ShLog_LevelName(ShLog_ConsoleLevel()));
        } else {
            ShLog_SetConsoleLevel(level);
            say(ctx, "log: console shows up to %s", ShLog_LevelName(level));
        }
        return done();
    }

    // A level alone sets every category.
    if (level_word.empty()) {
        if (const int level = ShLog_ParseLevel(what.c_str()); level >= 0) {
            ShLog_SetLevel(-1, level);
            say(ctx, "log: all categories %s", ShLog_LevelName(level));
            return done();
        }
    }

    const bool every = what == "*" || what == "all";
    const int category = every ? -1 : ShLog_ParseCategory(what.c_str());
    if (category < 0 && !every) {
        say(ctx, "log: unknown category '%s' - try 'log list'", what.c_str());
        return done();
    }
    if (level_word.empty()) {
        say(ctx, "log %s: %s", ShLog_CategoryName(category), ShLog_LevelName(ShLog_CategoryLevel(category)));
        return done();
    }
    const int level = ShLog_ParseLevel(level_word.c_str());
    if (level < 0) {
        say(ctx, "log: unknown level '%s' (off error warn info debug trace)", level_word.c_str());
        return done();
    }
    ShLog_SetLevel(category, level);
    say(ctx, "log: %s = %s%s", every ? "all" : ShLog_CategoryName(category), ShLog_LevelName(level),
        level > ShLog_ConsoleLevel() ? " (terminal/file; 'log screen' for here)" : "");
    return done();
}

void complete_log_targets(std::string_view, std::vector<Candidate>& out) {
    static std::vector<Candidate> cache;
    if (cache.empty()) {
        cache.push_back({"list", "categories and levels"});
        cache.push_back({"file", "log file path"});
        cache.push_back({"screen", "level of the lines shown here"});
        cache.push_back({"all", "every category"});
        for (int i = 0; i < SH_LOGCAT_COUNT; ++i) cache.push_back({ShLog_CategoryName(i), ShLog_CategoryHelp(i)});
        for (int i = 0; i <= SH_LOGLV_TRACE; ++i) cache.push_back({ShLog_LevelName(i), "level for all"});
    }
    out.insert(out.end(), cache.begin(), cache.end());
}

void complete_log_levels(std::string_view, std::vector<Candidate>& out) {
    static std::vector<Candidate> cache;
    if (cache.empty()) {
        for (int i = 0; i <= SH_LOGLV_TRACE; ++i) cache.push_back({ShLog_LevelName(i), {}});
    }
    out.insert(out.end(), cache.begin(), cache.end());
}

Result<void> run_fps(Context& ctx, const Args& args) {
    const std::string& mode = args.str(0);
    if (mode.size() == 1 && mode[0] >= '0' && mode[0] <= '4') {
        g_PcConfig.fpsDisplay = mode[0] - '0';
        say(ctx, "fps counter %s (fps_display in config.cfg keeps it)", g_PcConfig.fpsDisplay ? "on" : "off");
        return done();
    }
    float average = 0.0f, shortest = 0.0f, longest = 0.0f;
    int frames = 0;
    DbgTrace_FrameStats(&average, &shortest, &longest, &frames);
    if (frames == 0) {
        say(ctx, "fps: no frames measured yet");
        return done();
    }
    say(ctx, "fps %.1f  frame %.2f ms avg, %.2f min, %.2f max over %d frames%s",
        average > 0.0f ? 1000.0f / average : 0.0f, average, shortest, longest, frames,
        g_PcConsoleInputActive ? " (console open: game paused)" : "");
    say(ctx, "'fps 1..4' shows a counter in a corner, 'fps 0' hides it");
    return done();
}

Result<void> run_menustyle(Context& ctx, const Args& args) {
    static constexpr const char* kShown[2] = {"US", "JAPANESE"};
    const int before = g_PcConfig.menuStyle;
    const std::string& style = args.str(0);
    if (style == "auto") {
        g_PcConfig.menuStyle = sh::ui::kTitleStyleAuto;
    } else if (style == "us") {
        g_PcConfig.menuStyle = sh::ui::kTitleStyleUs;
    } else if (style == "japanese") {
        g_PcConfig.menuStyle = sh::ui::kTitleStyleJapanese;
    } else if (before < sh::ui::kTitleStyleUs) {  // no word: auto -> us -> japanese -> auto
        g_PcConfig.menuStyle = sh::ui::kTitleStyleUs;
    } else {
        g_PcConfig.menuStyle = before >= sh::ui::kTitleStyleJapanese ? sh::ui::kTitleStyleAuto : before + 1;
    }

    const int chosen = g_PcConfig.menuStyle;
    PcConfig_SaveKeyValue("menu_style", sh::ui::title_style_name(chosen));
    const int shown = sh::ui::title_art().style();
    if (chosen == sh::ui::kTitleStyleAuto) {
        say(ctx, "menu style AUTO - this disc's own, %s", kShown[shown]);
    } else {
        say(ctx, "menu style %s", kShown[shown]);
    }
    if (before != chosen && !sh::ui::title_art().reload()) say(ctx, "  shows next time the title screen is up");
    return done();
}

}  // namespace

void register_system_commands(Registry& registry) {
    add(registry, "quit", "system", "exit the game", {}, [](Context&, const Args&) -> Result<void> {
        SH_DBG("[CONSOLE] quit");
        std::exit(0);
    });

    add(registry, "restart", "system", "exit and start the game again with the same command line (a new set of mods loads)",
        {}, [](Context&, const Args&) -> Result<void> { return sh::mods::request_restart(); });

    add(registry, "crash", "system", "end the game with a real fault, to see the crash report it leaves",
        {arg("kind", ArgType::String, true, "segv (default): a write through a null pointer; abort: abort()",
             {"segv", "abort"})},
        [](Context&, const Args& args) -> Result<void> { sh::crash::provoke(args.joined(0)); });

    add(registry, "version", "system", "the engine version, this build and the mod API version", {},
        [](Context& ctx, const Args&) {
            ctx.out.info("Silent Hill PC engine " + std::string(sh::version::kText) + " (build " +
                         std::to_string(sh::version::build_number()) + ", commit " + sh::version::commit() + ", " +
                         sh::version::build_stamp() + ")");
            ctx.out.info("mod API " + std::to_string(sh::mods::kApiMajor) + "." + std::to_string(sh::mods::kApiMinor) +
                         "; mods require the engine with \"min_engine_version\" in mod.json");
            return done();
        });

    add(registry, "debug", "system", "debug key reference", {arg("page", ArgType::Int, true, "1 or 2")},
        [](Context& ctx, const Args& args) {
            if (args.str(0) == "2") {
                for (const char* line : kKeysPage2) say_text(ctx, line);
            } else {
                for (const char* line : kKeysPage1) say_text(ctx, line);
            }
            return done();
        });

    // The debug menu key's request, for scripts: with a menu id that menu opens, so runs without a keyboard
    // reach Options and the mods' pages.
    add(registry, "debugmenu", "ui", "open or close the DEBUG OPTIONS menu (the F10 menu, gamedata/menus)",
        {arg("menu", ArgType::String, true, "open this menu id instead (options, options.mods, a mod's page)")},
        [](Context& ctx, const Args& args) {
            const std::string menu = args.joined(0);
            if (menu.empty()) {
                Pc_DebugMenu_Request();
            } else if (!Pc_DebugMenu_OpenById(menu.c_str())) {
                say(ctx, "debugmenu: cannot open '%s' now", menu.c_str());
            }
            return done();
        });

    add(registry, "options", "ui", "open the Options screen from play or the title, or show one of its pages",
        {arg("page", ArgType::String, true, "a page of gamedata/menus/options.lua (display, camera, ...)")},
        [](Context& ctx, const Args& args) -> Result<void> {
            const std::string page = args.joined(0);
            if (Pc_Options_OpenFromConsole == nullptr || !Pc_Options_OpenFromConsole(page.c_str())) {
                ctx.out.error("options: opens from play, the title or on the Options screen");
                return Result<void>::fail("not available here");
            }
            return done();
        });

    // What Left / Right do on a setting row, for runs without a keyboard (the row need not be on screen).
    add(registry, "optstep", "ui", "step a setting row of the menus by its native name, as Left / Right on the row",
        {arg("native", ArgType::String, false, "the row's native (setting.quick_save_instant, setting.resolution)"),
         arg("dir", ArgType::Int, true, "-1 = left, 1 = right (default)")},
        [](Context& ctx, const Args& args) -> Result<void> {
            char native[96] = {0};
            int direction = 1;
            if (std::sscanf(args.joined(0).c_str(), "%95s %d", native, &direction) < 1) {
                return Result<void>::fail("optstep <native> [dir]");
            }
            const std::optional<std::string> now = sh::menu::registry().step_setting(native, direction < 0 ? -1 : 1);
            if (!now) {
                ctx.out.error("optstep: no setting native with that name");
                return Result<void>::fail("unknown setting");
            }
            ctx.out.info(std::string(native) + " = " + *now);
            return done();
        });

    add(registry, "winsize", "system", "resize the window as a window manager would (it becomes the saved resolution)",
        {arg("width", ArgType::Int, true, "window width"), arg("height", ArgType::Int, true, "window height")},
        [](Context& ctx, const Args& args) {
            const std::vector<std::string> words = spaced_words(args);
            const int width = words.size() >= 2 ? whole(words[0]) : 0;
            const int height = words.size() >= 2 ? whole(words[1]) : 0;
            const bool resize = width > 0 && height > 0;
            if (resize) sh::platform::resize_window(width, height);
            int now_w = 0, now_h = 0;
            sh::platform::window_size(&now_w, &now_h);
            say(ctx, resize ? "window %dx%d" : "window %dx%d; winsize <width> <height> resizes it", now_w, now_h);
            return done();
        });

    add(registry, "bind", "system", "run console commands from a key",
        {arg("key", ArgType::String, false, "key name, or 'list'", {"list"}),
         arg("commands", ArgType::Rest, true, "commands to run, ';' separated (quote the list)")},
        [](Context&, const Args& args) {
            sh::input::console_binds().command_bind(upper(args.joined(0)));
            return done();
        },
        {},
        "bind <key> <command>[; <command>...]   put several commands in quotes\n"
        "bind list                              show the binds\n"
        "unbind <key>, unbindall                remove them");
    add(registry, "unbind", "system", "remove a key bind", {arg("key", ArgType::String, false, "key name")},
        [](Context&, const Args& args) {
            sh::input::console_binds().command_unbind(upper(args.joined(0)));
            return done();
        });
    add(registry, "unbindall", "system", "remove every key bind", {}, [](Context&, const Args&) {
        sh::input::console_binds().command_unbind_all();
        return done();
    });

    add(registry, "log", "system", "log categories and levels",
        {arg("what", ArgType::String, true, "category, level, list, file, screen or all", {}, complete_log_targets),
         arg("level", ArgType::String, true, "off error warn info debug trace", {}, complete_log_levels)},
        run_log, {},
        "log                      list categories and levels\n"
        "log <category> [level]   show or set one category (all = every category)\n"
        "log <level>              set every category\n"
        "log screen [level]       level of the lines shown in the console\n"
        "log file                 path of the log file");

    add(registry, "fps", "system", "frame time statistics, or a corner counter",
        {arg("mode", ArgType::Int, true, "0 hides the counter, 1-4 picks a corner")}, run_fps);
    add(registry, "profiler", "system", "profiler overlay: frame time and 1% low, CPU, GPU, RAM, VRAM, draw calls",
        {arg("mode", ArgType::String, true, "on or off; toggles without it")}, [](Context& ctx, const Args& args) {
            const std::string mode = lower(args.joined(0));
            g_PcConfig.fpsProfiler = mode.empty() ? !g_PcConfig.fpsProfiler : (mode != "0" && mode != "off");
            PcConfig_SaveKeyValue("fps_profiler", g_PcConfig.fpsProfiler ? "1" : "0");
            say(ctx, "profiler overlay %s (fps_profiler in config.cfg keeps it)", g_PcConfig.fpsProfiler ? "on" : "off");
            return done();
        });

    add(registry, "conscale", "display", "Console text scale",
        {opt_integer("scale", "0..8; 0 = auto from the window height")}, [](Context& ctx, const Args& args) {
            if (args.has(0)) g_PcConfig.consoleScale = std::clamp(whole(args.str(0)), 0, 8);
            say(ctx, "console scale: %d (0=auto from window height; config key console_scale)",
                g_PcConfig.consoleScale);
            return done();
        });

    add(registry, "menustyle", "ui", "Main menu style",
        {arg("style", ArgType::Enum, true, "no argument steps auto -> us -> japanese", {"auto", "us", "japanese"})},
        run_menustyle, {},
        "Saved to the config (menu_style). Shows at the next title screen if it is not up now.");

    add(registry, "xavolume", "audio", "FMV and voice volume", {opt_number("percent", "0..100")},
        [](Context& ctx, const Args& args) {
            if (args.has(0)) {
                PcConfig_ApplyXaVolume(std::clamp(static_cast<float>(real(args.str(0))), 0.0f, 100.0f) / 100.0f);
            }
            say(ctx, "xa (fmv/voice) volume: %.0f%% (0..100)", sh::audio::voice_lines().volume() * 100.0f);
            return done();
        },
        {"xavol"});

    add(registry, "sounds", "audio", "what sounds now: the radio's static and the sound files playing",
        {}, [](Context& ctx, const Args&) {
            const sh::entities::game::RadioState radio = sh::entities::game::radio_state();
            say(ctx, "radio: %s", radio.on ? "on" : "off");
            for (std::size_t i = 0; i < radio.channels.size(); ++i) {
                const auto& ch = radio.channels[i];
                const sh::audio::SoundInfo* file = sh::audio::sound_swap(ch.sound);
                say(ctx, " channel %d: sound %d%s%s, follows slot %d, sounded for slot %d", static_cast<int>(i),
                    ch.sound, file != nullptr ? " = " : "", file != nullptr ? file->file.c_str() : "", ch.target,
                    ch.playing);
            }
            for (const auto& h : radio.heard) {
                if (h.slot >= 0) say(ctx, " in reach: slot %d, %u m^2", h.slot, h.distance_sq);
            }
            const std::vector<sh::audio::FilePlayer::VoiceInfo> files = sh::audio::audio_engine().file_voices();
            say(ctx, "sound files playing: %d", static_cast<int>(files.size()));
            for (const auto& v : files) {
                say(ctx, " sound %d: left %d, right %d of %d%s, level %d%%", v.tag, v.left, v.right,
                    sh::audio::FilePlayer::kUnity, v.loop ? ", loop" : "",
                    static_cast<int>(static_cast<std::int64_t>(v.fade) * 100 / sh::audio::FilePlayer::kFadeUnity));
            }
            int keyed = 0;
            for (int voice = 0; voice < sh::audio::kVoices; ++voice) {
                if (sh::audio::audio_engine().key_status(voice) != sh::audio::KeyStatus::Off) ++keyed;
            }
            say(ctx, "bank voices keyed: %d", keyed);
            return done();
        });
}

}  // namespace sh::debug::commands
