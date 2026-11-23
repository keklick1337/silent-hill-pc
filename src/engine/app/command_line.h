// SPDX-License-Identifier: GPL-3.0-or-later
//
// The arguments the game is started with.
#pragma once

#include <optional>
#include <string>

namespace sh::app {

struct CommandLine {
    enum class Action {
        Run,
        ShowHelp,          // -h, --help
        ShowVersion,       // --version
        InstallDesktop,    // --install-desktop
        UninstallDesktop,  // --uninstall-desktop
    };

    Action action = Action::Run;
    std::optional<std::string> data_dir;      // -data <dir>
    std::optional<std::string> image;         // --image <file>
    std::optional<std::string> renderer;      // -renderer <name>
    std::optional<std::string> command_file;  // -consolecmd <file>
    bool reimport = false;                    // --reimport
    bool quit_after_import = false;           // --quit-after-import
    bool dialogs_to_console = false;          // --dialogs-to-console
    bool skip_to_game = false;                // -skiptogame
    bool setup = false;                       // --setup
    std::optional<std::string> setup_script;  // --setup-script <file>
};

// Arguments the game does not know are ignored, and so is an option whose value is missing. The first request for
// the help or the version ends the reading: what follows it does not matter.
CommandLine parse_command_line(int argc, const char* const* argv);

// What --help prints after the version line.
const char* usage_text();

}  // namespace sh::app
