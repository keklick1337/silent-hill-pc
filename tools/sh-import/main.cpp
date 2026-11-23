#include "sh-import/run.h"

#ifdef SH_IMPORT_GAME_NUMBERS
#include "engine/ids/game_numbers.h"
#endif

#include <atomic>
#include <csignal>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::atomic<bool> g_cancel{false};

extern "C" void on_interrupt(int) {
    g_cancel.store(true);
}

}  // namespace

int main(int argc, char** argv) {
#ifdef SH_IMPORT_GAME_NUMBERS
    sh::ids::install_game_numbering();  // map data names flags and items as the game's own import does
#endif
    std::signal(SIGINT, on_interrupt);
    std::signal(SIGTERM, on_interrupt);
    const std::vector<std::string> args(argv + 1, argv + argc);
    return sh::import_tool::run(args, std::cout, std::cerr, stderr, &g_cancel);
}
