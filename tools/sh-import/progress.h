#pragma once

#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>

namespace sh::import_tool {

// "1:05" under an hour, "1:02:03" above, "-" for a negative or non-finite value.
std::string format_duration(double seconds);

// "[convert]  42.3%  elapsed 0:01:23  eta 0:01:50". `fraction` is the share of the whole import; the ETA is the
// elapsed time scaled by what is left, and "eta ?" until there is enough progress to say.
std::string format_progress_line(std::string_view stage, double fraction, double elapsed_seconds);

// The ProgressFn of import_image(): one status line on a stream. On a terminal the line is rewritten in place about
// every 250 ms; otherwise a new line is printed at most every 5 s and on every stage change.
class ProgressPrinter {
public:
    ProgressPrinter(std::FILE* stream, bool terminal);

    void operator()(std::string_view stage, double fraction);

    // Ends the status line (a newline on a terminal). Call once after the import.
    void finish();

private:
    std::FILE* stream_;
    bool terminal_;
    bool line_open_ = false;
    std::string stage_;
    std::chrono::steady_clock::time_point start_;
    std::chrono::steady_clock::time_point last_print_;
    bool printed_ = false;
};

}  // namespace sh::import_tool
