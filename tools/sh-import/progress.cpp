#include "sh-import/progress.h"

#include <algorithm>
#include <cmath>

namespace sh::import_tool {

std::string format_duration(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) {
        return "-";
    }
    const auto total = static_cast<unsigned long long>(seconds + 0.5);
    const unsigned long long h = total / 3600;
    const unsigned long long m = (total / 60) % 60;
    const unsigned long long s = total % 60;
    char buffer[48];
    if (h > 0) {
        std::snprintf(buffer, sizeof buffer, "%llu:%02llu:%02llu", h, m, s);
    } else {
        std::snprintf(buffer, sizeof buffer, "%llu:%02llu", m, s);
    }
    return buffer;
}

std::string format_progress_line(std::string_view stage, double fraction, double elapsed_seconds) {
    fraction = std::clamp(fraction, 0.0, 1.0);
    std::string eta = "?";
    if (fraction >= 1.0) {
        eta = "0:00";
    } else if (fraction >= 0.01 && elapsed_seconds >= 1.0) {
        eta = format_duration(elapsed_seconds * (1.0 - fraction) / fraction);
    }
    char head[48];
    std::snprintf(head, sizeof head, "%5.1f%%", fraction * 100.0);
    return "[" + std::string(stage) + "] " + head + "  elapsed " + format_duration(elapsed_seconds) + "  eta " + eta;
}

ProgressPrinter::ProgressPrinter(std::FILE* stream, bool terminal)
    : stream_(stream), terminal_(terminal), start_(std::chrono::steady_clock::now()), last_print_(start_) {}

void ProgressPrinter::operator()(std::string_view stage, double fraction) {
    const auto now = std::chrono::steady_clock::now();
    const bool stage_changed = stage_ != stage;
    const auto interval = terminal_ ? std::chrono::milliseconds(250) : std::chrono::milliseconds(5000);
    if (printed_ && !stage_changed && fraction < 1.0 && now - last_print_ < interval) {
        return;
    }
    const double elapsed = std::chrono::duration<double>(now - start_).count();
    const std::string line = format_progress_line(stage, fraction, elapsed);
    if (terminal_) {
        // Keeps the finished stages on screen: a stage change starts a new line.
        if (stage_changed && line_open_) {
            std::fputc('\n', stream_);
        }
        std::fprintf(stream_, "\r%s\033[K", line.c_str());
        line_open_ = true;
    } else {
        std::fprintf(stream_, "%s\n", line.c_str());
    }
    std::fflush(stream_);
    stage_ = stage;
    last_print_ = now;
    printed_ = true;
}

void ProgressPrinter::finish() {
    if (terminal_ && line_open_) {
        std::fputc('\n', stream_);
        std::fflush(stream_);
        line_open_ = false;
    }
}

}  // namespace sh::import_tool
