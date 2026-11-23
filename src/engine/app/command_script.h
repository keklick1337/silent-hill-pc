// SPDX-License-Identifier: GPL-3.0-or-later
//
// The file of console commands given with -consolecmd (automation: tests/smoke, the replay routes): one command a
// line, run in order once the game is up. A line may begin with a wait:
//   @<seconds> <command>   not before this many seconds after the start (the wall clock; in a replay the game's
//                          own clock, so a command lands on the same tick every run);
//   @+<n> <command>        <n> sixtieths of a second of play after the previous command ran, counted by the
//                          game's own step while the player is in control, so a check lands on the same game
//                          frame at any frame rate.
// A wait without a command only holds back what follows. Empty lines and lines starting with # are skipped.
#pragma once

#include <cstdlib>
#include <string>
#include <string_view>

namespace sh::app {

// One line of the file.
struct ScriptLine {
    enum class Wait { None, Seconds, PlayTime };

    Wait wait = Wait::None;
    float seconds = 0.0f;      // Wait::Seconds
    int sixtieths = 0;         // Wait::PlayTime
    std::string command;       // empty: only a wait

    // False for a line that is neither a command nor a wait (empty, a comment).
    static bool parse(std::string_view text, ScriptLine& out) {
        while (!text.empty() && (text.back() == '\r' || text.back() == '\n')) {
            text.remove_suffix(1);
        }
        if (text.empty() || text.front() == '#') {
            return false;
        }
        out = ScriptLine{};
        if (text.front() == '@') {
            const std::size_t end = text.find_first_of(" \t");
            const std::string number(text.substr(1, end == std::string_view::npos ? end : end - 1));
            if (!number.empty() && number.front() == '+' && std::atoi(number.c_str() + 1) >= 0) {
                out.wait = Wait::PlayTime;
                out.sixtieths = std::atoi(number.c_str() + 1);
            } else {
                out.wait = Wait::Seconds;
                out.seconds = static_cast<float>(std::atof(number.c_str()));
            }
            const std::size_t command = text.find_first_not_of(" \t", end);
            text = end == std::string_view::npos || command == std::string_view::npos ? std::string_view{}
                                                                                      : text.substr(command);
        }
        out.command = std::string(text);
        return true;
    }
};

// The file to run; none by default. Set once, before the game loop starts.
void set_command_script(const std::string& file);

// Runs at most one command that is due. Called once a frame of the game loop, where the renderer and the map are
// alive.
void command_script_frame();

// The same from every presented frame, for screenshots only: the boot screens and the movies present outside the
// game loop, and a screenshot due there must not come late.
void command_script_present();

}  // namespace sh::app
