// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/app/command_line.h"

#include <string_view>

namespace sh::app {

CommandLine parse_command_line(int argc, const char* const* argv) {
    CommandLine line;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        // An option with a value takes the next argument, when there is one.
        const auto value_into = [&](std::optional<std::string>& out) {
            if (i + 1 < argc) {
                out = argv[++i];
            }
        };
        if (arg == "-data") {
            value_into(line.data_dir);
        } else if (arg == "--image") {
            value_into(line.image);
        } else if (arg == "-renderer") {
            value_into(line.renderer);
        } else if (arg == "-consolecmd") {
            value_into(line.command_file);
        } else if (arg == "--reimport") {
            line.reimport = true;
        } else if (arg == "--quit-after-import") {
            line.quit_after_import = true;
        } else if (arg == "--dialogs-to-console") {
            line.dialogs_to_console = true;
        } else if (arg == "--setup") {
            line.setup = true;
        } else if (arg == "--setup-script") {
            value_into(line.setup_script);
        } else if (arg == "-skiptogame") {
            line.skip_to_game = true;
        } else if (arg == "--install-desktop") {
            line.action = CommandLine::Action::InstallDesktop;
        } else if (arg == "--uninstall-desktop") {
            line.action = CommandLine::Action::UninstallDesktop;
        } else if (arg == "--version") {
            line.action = CommandLine::Action::ShowVersion;
            break;
        } else if (arg == "-h" || arg == "--help") {
            line.action = CommandLine::Action::ShowHelp;
            break;
        }
    }
    return line;
}

const char* usage_text() {
    return "Usage: SilentHillPC [options]\n"
           "Options:\n"
           "  -data <path>         Game data directory (default: gamedata next to the executable)\n"
           "  --image <path>       Disc image (.cue/.bin) to import on the first start\n"
           "  --reimport           Import the disc image again, replacing the game data\n"
           "  --quit-after-import  Import the disc image if the game data needs it, then exit without\n"
           "                       starting the game (exit code 0: the game data is ready, 1: no image or\n"
           "                       the import failed, 2: cancelled)\n"
           "  --setup              Show the quick setup (language, renderer, graphics, display) before\n"
           "                       the game, as on the first start\n"
           "  --setup-script <file> Tests: show the quick setup and drive it from the commands of a file\n"
           "                       also on the offscreen video driver\n"
           "  -skiptogame          Start on the map the config names, past the title\n"
           "  -renderer <name>     Use this renderer instead of the config's (gl, vulkan, sdlgpu)\n"
           "  -consolecmd <file>   Run the console commands of a file once the game is up\n"
           "  --dialogs-to-console Print error dialogs instead of showing them\n"
           "  --install-desktop    Linux: add the game to this user's applications menu\n"
           "                       (~/.local/share/applications/silent-hill-pc.desktop and the icons), then exit\n"
           "  --uninstall-desktop  Linux: remove that menu entry and the icons, then exit\n"
           "  --version            Print the engine version and exit\n"
           "  -h, --help           Show this help\n";
}

}  // namespace sh::app
