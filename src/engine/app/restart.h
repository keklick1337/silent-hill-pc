// Restarting the game in place (a new set of mods loads only at start): the process exits the normal way, so
// every atexit teardown (renderer, audio, log) runs, and the last hook starts the same executable again with the
// arguments it was given. On Linux/macOS the process image is replaced (same pid, same terminal: stdin/stdout/
// stderr as the game was started with them, every other descriptor closed); on Windows the old process starts
// the new one and, when it has a console, hands it over and waits, exiting with the new game's code.
#pragma once

#include "formats/common/status.h"

namespace sh::app {

// Keeps the command line for restart() and installs the exit hook. Call first thing in main(): atexit hooks run
// in reverse order, so this one runs after every teardown registered later.
void remember_command_line(int argc, char** argv);

// Exits the process and starts it again with the same arguments. Returns only when it cannot (no command line
// remembered); a failure to start the new process is logged on the way out.
Result<void> restart();

// Windows: attaches to the console a restarting game handed over (true), instead of opening a new one. False
// everywhere else and when nothing was handed over.
bool take_over_console();

}  // namespace sh::app
