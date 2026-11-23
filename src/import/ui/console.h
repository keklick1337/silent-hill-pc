#pragma once

#include "import/ui/controller.h"

#include <chrono>
#include <functional>
#include <iosfwd>
#include <string>

namespace sh::import::ui {

struct ConsoleOptions {
    // Image path given on the command line: used for the first question without asking.
    std::string image_arg;
    // Polled while the import runs (a SIGINT flag, say); true cancels the import.
    std::function<bool()> interrupted;
    std::chrono::milliseconds poll{100};
};

// Exit codes of run_console.
constexpr int kConsoleOk = 0;         // imported, or nothing to import
constexpr int kConsoleFailed = 1;     // the import failed
constexpr int kConsoleCancelled = 2;  // cancelled, quit, or the input ended

// Drives `controller` with text: asks for the path on `out`, reads answers from `in` (an empty answer takes the
// suggested path, q quits), prints one line per progress step, and ends with the result. Calls controller.start().
int run_console(Controller& controller, std::istream& in, std::ostream& out, const ConsoleOptions& options = {});

}  // namespace sh::import::ui
