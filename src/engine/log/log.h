// SPDX-License-Identifier: GPL-3.0-or-later
// C++ side of the journal. C code and map overlays use the facade in sh_log.h.
#pragma once

#include "engine/log/sh_log.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace sh::log {

enum class Level : unsigned char { Off = 0, Error, Warn, Info, Debug, Trace };

inline constexpr int kCategoryCount = SH_LOGCAT_COUNT;
inline constexpr int kKeepLogFiles  = 10;
inline constexpr std::size_t kConsoleQueueMax = 1024;

int category_from_tag(std::string_view text);
int parse_level(std::string_view text);
int parse_category(std::string_view text);
const char* level_name(int level);
const char* category_name(int category);
const char* category_help(int category);

// Keeps the newest kKeepLogFiles - 1 files named silenthill-*.log in dir, so
// the one about to be created makes kKeepLogFiles.
void rotate_logs(const std::filesystem::path& dir, int keep);

// The log file as a crash handler has to finish it, without stdio: its descriptor (-1: no log file) and the bytes
// stdio holds for it but has not written yet (none where the C library does not tell how many there are). Reads
// plain words only, so a signal handler may call it.
struct FileTail
{
    int descriptor = -1;
    const char* bytes = nullptr;
    std::size_t size = 0;
};
FileTail unwritten_file_tail() noexcept;

class Logger
{
public:
    static Logger& instance();

    Logger() { level_.fill(static_cast<unsigned char>(Level::Info)); }

    void init();
    bool config_key(std::string_view key, std::string_view value);
    void set_legacy_debug_log(bool on);
    bool file_wanted() const { return file_wanted_; }
    bool start_file(const std::filesystem::path& exe_dir);
    const std::string& file_path() const { return file_path_; }
    void set_terminal(std::FILE* stream);
    void set_frame(unsigned frame) { frame_.store(frame, std::memory_order_relaxed); }
    void flush();
    void periodic_flush();
    void shutdown();

    int apply_spec(std::string_view spec);
    bool set_level(int category, int level);
    int category_level(int category) const;
    int console_level() const { return console_level_; }
    void set_console_level(int level);

    void write(int category, int level, const char* fmt, std::va_list ap, bool echo);
    void drain_console(ShLogConsoleFn fn);
    void set_listener(ShLogListenerFn fn) { listener_ = fn; }
    // The newest lines that passed their category's level (as the terminal prints them), oldest first, at most
    // `max` of the kRecentLines kept: a state dump carries them (`dump`).
    static constexpr std::size_t kRecentLines = 256;
    std::vector<std::string> recent_lines(std::size_t max) const;

private:
    struct ConsoleLine
    {
        std::string text;
        unsigned char level;
        bool toast;
    };

    void recompute_gates();
    void deliver(int category, int level, std::string_view message, bool echo);
    std::string stamp() const;

    std::array<unsigned char, kCategoryCount> level_{};
    int base_level_     = static_cast<int>(Level::Info);
    int file_floor_     = 0;  // file also takes up to this level (enable_debug_log)
    int console_level_  = static_cast<int>(Level::Info);
    bool file_wanted_   = false;
    bool file_explicit_ = false;
    bool terminal_on_   = true;
    int color_mode_     = -1;  // -1 auto, 0 off, 1 on
    bool color_         = false;

    std::FILE* terminal_ = nullptr;
    std::FILE* file_     = nullptr;
    std::string file_path_;
    std::atomic<unsigned> frame_{0};
    std::chrono::steady_clock::time_point last_flush_{};

    mutable std::mutex mutex_;
    std::deque<ConsoleLine> console_;
    std::deque<std::string> recent_;
    ShLogListenerFn listener_ = nullptr;
};

}  // namespace sh::log
