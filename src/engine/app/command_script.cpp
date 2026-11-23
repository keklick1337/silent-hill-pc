// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/app/command_script.h"

#include <cstdint>
#include <fstream>
#include <initializer_list>

#include <SDL3/SDL.h>

#include "engine/debug/console_host.h"
#include "engine/log/sh_log.h"
#include "engine/app/loop/frame_loop.h"
#include "engine/loop/frame_view.h"
#include "engine/timing/vblank.h"


namespace sh::app {

namespace {

// Commands that may run from a presented frame outside the game loop. The window size is among them: a window
// manager resizing the window during a movie is what it stands in for.
bool runs_while_presenting(const std::string& command) {
    for (const std::string_view name : {"shot", "screenshot", "shotwin", "winsize"}) {
        if (command.size() >= name.size() && SDL_strncasecmp(command.c_str(), name.data(), name.size()) == 0 &&
            (command.size() == name.size() || command[name.size()] == ' ' || command[name.size()] == '\t')) {
            return true;
        }
    }
    return false;
}

class CommandScript {
public:
    void set_file(const std::string& file) { path_ = file; }

    void step(bool presenting) {
        if (path_.empty() || state_ == State::Done) {
            return;
        }
        if (!presenting && sh::loop::in_play()) {
            count_play_time();
        }
        if (state_ == State::NotOpened) {
            open();
            return;
        }
        const float elapsed = seconds_since_start();
        for (;;) {
            if (!have_line_ && !next_line()) {
                return;
            }
            if (!due(presenting, elapsed)) {
                return;
            }
            if (line_.command.empty()) {
                // A bare wait is over: read on.
                consumed();
                continue;
            }
            if (presenting && !runs_while_presenting(line_.command)) {
                return;
            }
            consumed();
            Pc_ConsoleExec(line_.command.c_str());
            return;  // one command a call keeps the console's output readable
        }
    }

private:
    enum class State { NotOpened, Running, Done };

    void open() {
        file_.open(path_);
        state_ = file_ ? State::Running : State::Done;
        start_ns_ = SDL_GetTicksNS();
        if (state_ == State::Done) {
            SH_LOG_WARN(CONSOLE, "[CONSOLECMD] cannot open %s", path_.c_str());
        }
    }

    // The game's own step, summed while the player is in control. A scripted check compares runs update by
    // update, so a frame that took a longer step is told in the log.
    void count_play_time() {
        const int step = sh::loop::frame_delta();
        if (last_step_ != 0 && step != last_step_) {
            // tests/smoke/fps_ab.py counts these lines.
            SH_LOG_INFO(CONSOLE, "[CONSOLECMD] step %d -> %d at update %d", last_step_, step,
                        static_cast<int>(sh::loop::frame_ticks()));
        }
        last_step_ = step;
        play_time_ += step;
    }

    // Seconds, not frames: the boot screens and the movies present outside the game loop, slow renderers draw
    // fewer frames, and the scripts' callers stop the game at a wall-clock limit. In a replay the game's own clock.
    float seconds_since_start() {
        namespace clock = sh::timing::virtual_time;
        if (!clock::active()) {
            return static_cast<float>(static_cast<double>(SDL_GetTicksNS() - start_ns_) / 1e9);
        }
        if (virtual_start_ms_ == 0) {
            virtual_start_ms_ = clock::ticks_ms() + 1;  // never 0, which means "not taken yet"
        }
        return static_cast<float>(clock::ticks_ms() + 1 - virtual_start_ms_) / 1000.0f;
    }

    bool next_line() {
        std::string text;
        while (std::getline(file_, text)) {
            if (ScriptLine::parse(text, line_)) {
                have_line_ = true;
                return true;
            }
        }
        file_.close();
        state_ = State::Done;
        return false;
    }

    bool due(bool presenting, float elapsed) const {
        if (line_.wait != ScriptLine::Wait::PlayTime) {
            return elapsed >= line_.seconds;
        }
        // Play time (4096 units a second) advances only in the game loop's frames.
        return !presenting &&
               (play_time_ - last_run_) * 60 >= static_cast<std::int64_t>(line_.sixtieths) * 4096;
    }

    void consumed() {
        have_line_ = false;
        last_run_ = play_time_;
    }

    std::string path_;
    std::ifstream file_;
    State state_ = State::NotOpened;
    std::uint64_t start_ns_ = 0;
    std::uint32_t virtual_start_ms_ = 0;
    ScriptLine line_;
    bool have_line_ = false;
    int last_step_ = 0;
    std::int64_t play_time_ = 0;
    std::int64_t last_run_ = 0;
};

CommandScript& script() {
    static CommandScript s;
    return s;
}

}  // namespace

void set_command_script(const std::string& file) { script().set_file(file); }
void command_script_frame() { script().step(false); }
void command_script_present() { script().step(true); }

}  // namespace sh::app
