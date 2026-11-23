// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/log/log.h"
#include "engine/manifest/map_manifest.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <system_error>
#include <vector>

#if defined(__linux__) && __has_include(<stdio_ext.h>)
#include <stdio_ext.h>
#define SH_LOG_HAS_FPENDING 1
#endif

#ifdef _WIN32
#include <io.h>
#define SH_ISATTY _isatty
#define SH_FILENO _fileno
#else
#include <unistd.h>
#define SH_ISATTY isatty
#define SH_FILENO fileno
#endif

extern "C" {
unsigned char g_ShLogGate[SH_LOGCAT_COUNT];
unsigned char g_ShLogGateAny = 0;
FILE* g_ShDebugLog = nullptr;
}

namespace sh::log {
namespace {

// The log file's stdio buffer. At namespace scope: unwritten_file_tail() hands its unwritten start to a crash
// handler.
char g_file_buffer[64 * 1024];

// The log file name comes from the game manifest (log_prefix) with the
// port's own fallback when gamedata/ is absent.
std::string log_file_prefix() {
    const char* p = MapManifest_String("log_prefix");
    return p != nullptr ? std::string(p) + "-" : std::string("silenthill-");
}

struct CategoryInfo
{
    const char* name;
    const char* aliases;  // comma separated
    const char* help;
};

constexpr std::array<CategoryInfo, kCategoryCount> kCategories{{
    {"game", "general,sh", "everything without a narrower category"},
    {"fs", "vfs,file,files", "file reads, fs queue, loose/mod files"},
    {"cd", "", "CD sector reads"},
    {"texture", "tex,tim,textures", "TIM uploads, replacement pictures"},
    {"model", "models,mesh", "character models, LM/IPD/TMD"},
    {"anim", "anims,animation", "animation files"},
    {"spawn", "chara,charas,npc", "characters spawned and removed"},
    {"event", "flag,flags,story,events", "story flags, game/system states"},
    {"map", "overlay,room,maps", "map changes, overlays, rooms"},
    {"fmv", "video,movie", "movies"},
    {"audio", "xa,bgm,sound,sfx", "XA voice, BGM, SFX"},
    {"input", "pad,bind,binds", "pads, binds, mouse"},
    {"render", "gl,gpu,perf", "GL, effects, frame timing"},
    {"save", "memcard,quicksave", "memory card, quick save"},
    {"import", "", "disc image import"},
    {"config", "cfg", "config.cfg"},
    {"console", "cmd,cheat", "console commands, cheats"},
}};

// Leading "[TAG]" of the legacy messages -> category. First match wins, so a
// longer tag sits before the shorter one it starts with (POOLTEX before POOL).
struct TagRule
{
    const char* tag;
    int category;
};

constexpr TagRule kTagRules[] = {
    {"FSQ", SH_LOGCAT_FS},              {"LOOSE/HIRES", SH_LOGCAT_TEXTURE}, {"LOOSE", SH_LOGCAT_FS},
    {"VFS", SH_LOGCAT_FS},              {"FS", SH_LOGCAT_FS},               {"CD", SH_LOGCAT_CD},
    {"XA", SH_LOGCAT_AUDIO},            {"SH_AMB", SH_LOGCAT_AUDIO},        {"BGM", SH_LOGCAT_AUDIO},
    {"SFX", SH_LOGCAT_AUDIO},           {"MSGVOICE", SH_LOGCAT_AUDIO},      {"AUDIO", SH_LOGCAT_AUDIO},
    {"TEX", SH_LOGCAT_TEXTURE},         {"HIRES", SH_LOGCAT_TEXTURE},       {"POOLTEX", SH_LOGCAT_TEXTURE},
    {"DDS", SH_LOGCAT_TEXTURE},         {"CHARA-TEX", SH_LOGCAT_TEXTURE},   {"TIM", SH_LOGCAT_TEXTURE},
    {"FONT", SH_LOGCAT_TEXTURE},        {"GLYPHS", SH_LOGCAT_TEXTURE},      {"POOLSTOMP", SH_LOGCAT_TEXTURE},
    {"POOL", SH_LOGCAT_SPAWN},          {"SPAWN", SH_LOGCAT_SPAWN},         {"CHARA", SH_LOGCAT_SPAWN},
    {"NPC", SH_LOGCAT_SPAWN},           {"BIGTMD", SH_LOGCAT_MODEL},        {"BIGLM", SH_LOGCAT_MODEL},
    {"LM", SH_LOGCAT_MODEL},            {"MODEL", SH_LOGCAT_MODEL},         {"ANIM", SH_LOGCAT_ANIM},
    {"RELOAD", SH_LOGCAT_ANIM},         {"KF", SH_LOGCAT_ANIM},             {"MAP", SH_LOGCAT_MAP},
    {"TRANSITION", SH_LOGCAT_MAP},      {"WHOLEMAP", SH_LOGCAT_MAP},        {"ROOM", SH_LOGCAT_MAP},
    {"SYSSTATE", SH_LOGCAT_EVENT},      {"GP-STATE", SH_LOGCAT_EVENT},      {"EVENT", SH_LOGCAT_EVENT},
    {"FLAG", SH_LOGCAT_EVENT},          {"TITLE", SH_LOGCAT_EVENT},         {"CREDITS", SH_LOGCAT_EVENT},
    {"FMV", SH_LOGCAT_FMV},             {"MDEC", SH_LOGCAT_FMV},            {"BIND", SH_LOGCAT_INPUT},
    {"CTRLSTYLE", SH_LOGCAT_INPUT},     {"PAD", SH_LOGCAT_INPUT},           {"INPUT", SH_LOGCAT_INPUT},
    {"GL", SH_LOGCAT_RENDER},           {"DECAL", SH_LOGCAT_RENDER},        {"FX", SH_LOGCAT_RENDER},
    {"PERF", SH_LOGCAT_RENDER},         {"SLOWFRAME", SH_LOGCAT_RENDER},    {"FLICK", SH_LOGCAT_RENDER},
    {"GPUBENCH", SH_LOGCAT_RENDER},     {"FPSCAM", SH_LOGCAT_RENDER},       {"FREECAM", SH_LOGCAT_RENDER},
    {"CAMSNAP", SH_LOGCAT_RENDER},      {"RENDER", SH_LOGCAT_RENDER},       {"PGXP", SH_LOGCAT_RENDER},
    {"SHADOW", SH_LOGCAT_RENDER},       {"SAVE", SH_LOGCAT_SAVE},           {"WARMRESET", SH_LOGCAT_SAVE},
    {"CFG", SH_LOGCAT_CONFIG},          {"CONFIG", SH_LOGCAT_CONFIG},       {"CONSOLE", SH_LOGCAT_CONSOLE},
    {"CHEAT", SH_LOGCAT_CONSOLE},       {"DEBUG", SH_LOGCAT_CONSOLE},       {"IMPORT", SH_LOGCAT_IMPORT},
};

constexpr const char* kLevelNames[] = {"off", "error", "warn", "info", "debug", "trace"};
constexpr char kLevelLetters[] = {'-', 'E', 'W', 'I', 'D', 'T'};

bool iequals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

bool istarts_with(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && iequals(text.substr(0, prefix.size()), prefix);
}

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

int clamp_level(int level)
{
    return std::clamp(level, 0, static_cast<int>(Level::Trace));
}

std::string vformat(const char* fmt, std::va_list ap)
{
    char small[1024];
    std::va_list copy;
    va_copy(copy, ap);
    const int n = std::vsnprintf(small, sizeof(small), fmt, copy);
    va_end(copy);
    if (n < 0)
        return {};
    if (static_cast<std::size_t>(n) < sizeof(small))
        return std::string(small, static_cast<std::size_t>(n));
    std::string big(static_cast<std::size_t>(n) + 1, '\0');
    std::vsnprintf(big.data(), big.size(), fmt, ap);
    big.resize(static_cast<std::size_t>(n));
    return big;
}

const char* level_color(int level)
{
    switch (level)
    {
        case SH_LOGLV_ERROR: return "\x1b[1;31m";
        case SH_LOGLV_WARN: return "\x1b[33m";
        case SH_LOGLV_DEBUG: return "\x1b[36m";
        case SH_LOGLV_TRACE: return "\x1b[90m";
        default: return "";
    }
}

}  // namespace

int category_from_tag(std::string_view text)
{
    text = trim(text);
    if (text.empty() || text.front() != '[')
        return SH_LOGCAT_GAME;
    text.remove_prefix(1);
    const auto close = text.find(']');
    if (close == std::string_view::npos)
        return SH_LOGCAT_GAME;
    const std::string_view tag = trim(text.substr(0, close));
    for (const TagRule& rule : kTagRules)
    {
        if (istarts_with(tag, rule.tag))
            return rule.category;
    }
    return SH_LOGCAT_GAME;
}

int parse_level(std::string_view text)
{
    text = trim(text);
    for (int i = 0; i <= static_cast<int>(Level::Trace); ++i)
    {
        if (iequals(text, kLevelNames[i]))
            return i;
    }
    if (iequals(text, "warning"))
        return SH_LOGLV_WARN;
    if (iequals(text, "err"))
        return SH_LOGLV_ERROR;
    if (iequals(text, "none") || iequals(text, "0"))
        return SH_LOGLV_OFF;
    if (text.size() == 1 && text[0] >= '1' && text[0] <= '5')
        return text[0] - '0';
    return -1;
}

int parse_category(std::string_view text)
{
    text = trim(text);
    for (int i = 0; i < kCategoryCount; ++i)
    {
        if (iequals(text, kCategories[i].name))
            return i;
        std::string_view aliases = kCategories[i].aliases;
        while (!aliases.empty())
        {
            const auto comma = aliases.find(',');
            if (iequals(text, aliases.substr(0, comma)))
                return i;
            if (comma == std::string_view::npos)
                break;
            aliases.remove_prefix(comma + 1);
        }
    }
    return -1;
}

const char* level_name(int level)
{
    return kLevelNames[clamp_level(level)];
}

const char* category_name(int category)
{
    return (category >= 0 && category < kCategoryCount) ? kCategories[category].name : "?";
}

const char* category_help(int category)
{
    return (category >= 0 && category < kCategoryCount) ? kCategories[category].help : "";
}

void rotate_logs(const std::filesystem::path& dir, int keep)
{
    std::error_code ec;
    std::vector<std::filesystem::path> logs;
    const std::string prefix = log_file_prefix();
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
    {
        const std::string name = entry.path().filename().string();
        if (entry.is_regular_file(ec) && name.rfind(prefix, 0) == 0 && name.size() > 4 &&
            name.compare(name.size() - 4, 4, ".log") == 0)
            logs.push_back(entry.path());
    }
    // Names carry the start time (silenthill-YYYYMMDD-HHMMSS[-n].log), so
    // name order is age order.
    std::sort(logs.begin(), logs.end());
    const std::size_t room = keep > 0 ? static_cast<std::size_t>(keep - 1) : 0;
    for (std::size_t i = 0; logs.size() > room && i < logs.size() - room; ++i)
        std::filesystem::remove(logs[i], ec);
}

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

void Logger::init()
{
    level_.fill(static_cast<unsigned char>(base_level_));
#ifndef _WIN32
    // stdout/stderr are redirected to the log file or the null device once the
    // config is read (game_main.cpp); the terminal sink keeps its own descriptor.
    const int fd = dup(SH_FILENO(stderr));
    if (fd >= 0)
    {
        terminal_ = fdopen(fd, "w");
        if (terminal_)
            std::setvbuf(terminal_, nullptr, _IOLBF, 4096);
        else
            close(fd);
    }
#endif
    set_terminal(terminal_);
    recompute_gates();
}

void Logger::set_terminal(std::FILE* stream)
{
    std::lock_guard lock(mutex_);
    terminal_ = stream;
    const char* no_color = std::getenv("NO_COLOR");
    if (color_mode_ >= 0)
        color_ = color_mode_ == 1;
    else
        color_ = stream && SH_ISATTY(SH_FILENO(stream)) && !(no_color && no_color[0]);
#ifdef _WIN32
    if (color_mode_ < 0)
        color_ = false;
#endif
}

bool Logger::config_key(std::string_view key, std::string_view value)
{
    value = trim(value);
    if (key == "log_level")
    {
        const int lv = parse_level(value);
        if (lv < 0)
        {
            ShLog_Write(SH_LOGCAT_CONFIG, SH_LOGLV_WARN, "log_level: unknown level '%.*s'",
                        static_cast<int>(value.size()), value.data());
            return true;
        }
        base_level_ = lv;
        level_.fill(static_cast<unsigned char>(lv));
    }
    else if (key == "log_categories")
    {
        if (apply_spec(value) > 0)
            ShLog_Write(SH_LOGCAT_CONFIG, SH_LOGLV_WARN, "log_categories: entries not understood in '%.*s'",
                        static_cast<int>(value.size()), value.data());
    }
    else if (key == "log_file")
    {
        file_wanted_   = value == "1" || iequals(value, "on") || iequals(value, "true");
        file_explicit_ = true;
    }
    else if (key == "log_file_level")
    {
        const int lv = parse_level(value);
        if (lv >= 0)
            file_floor_ = lv;
    }
    else if (key == "log_terminal")
    {
        terminal_on_ = !(value == "0" || iequals(value, "off") || iequals(value, "false"));
    }
    else if (key == "log_color")
    {
        color_mode_ = iequals(value, "auto") ? -1 : (value == "0" || iequals(value, "off")) ? 0 : 1;
        set_terminal(terminal_);
    }
    else if (key == "log_console_level")
    {
        const int lv = parse_level(value);
        if (lv >= 0)
            console_level_ = lv;
    }
    else
    {
        return false;
    }
    recompute_gates();
    return true;
}

void Logger::set_legacy_debug_log(bool on)
{
    // enable_debug_log = 1 meant "SilentHill.log with every SH_DBG line": the
    // file keeps that unless log_file / log_file_level say otherwise.
    if (!on)
        return;
    if (!file_explicit_)
        file_wanted_ = true;
    if (file_floor_ == 0)
        file_floor_ = SH_LOGLV_DEBUG;
    recompute_gates();
}

bool Logger::start_file(const std::filesystem::path& exe_dir)
{
    if (file_)
        return true;
    std::error_code ec;
    const std::filesystem::path dir = exe_dir / "logs";
    std::filesystem::create_directories(dir, ec);
    rotate_logs(dir, kKeepLogFiles);

    char stamp_buf[32];
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    std::strftime(stamp_buf, sizeof(stamp_buf), "%Y%m%d-%H%M%S", &tm);

    std::filesystem::path path = dir / (log_file_prefix() + std::string(stamp_buf) + ".log");
    for (int n = 2; std::filesystem::exists(path, ec) && n < 100; ++n)
        path = dir / (log_file_prefix() + std::string(stamp_buf) + "-" + std::to_string(n) + ".log");

    // Append mode: stdout/stderr are redirected into the same file with "a",
    // and O_APPEND on both keeps one writer from overwriting the other.
    std::FILE* f = std::fopen(path.string().c_str(), "a");
    if (!f)
        return false;
    // Fully buffered: per-line flushing halved the frame rate under heavy
    // debug logging. Errors and Sh_LogPeriodicFlush flush it.
    std::setvbuf(f, g_file_buffer, _IOFBF, sizeof(g_file_buffer));
    {
        std::lock_guard lock(mutex_);
        file_      = f;
        file_path_ = path.string();
    }
    g_ShDebugLog = f;
    recompute_gates();
    return true;
}

FileTail unwritten_file_tail() noexcept
{
    FileTail tail;
    std::FILE* const file = g_ShDebugLog;
    if (file == nullptr)
        return tail;
    tail.descriptor = SH_FILENO(file);
#ifdef SH_LOG_HAS_FPENDING
    // A write stream keeps its pending bytes at the start of its buffer.
    tail.bytes = g_file_buffer;
    tail.size  = std::min(__fpending(file), sizeof(g_file_buffer));
#endif
    return tail;
}

void Logger::flush()
{
    std::lock_guard lock(mutex_);
    if (file_)
        std::fflush(file_);
    if (terminal_)
        std::fflush(terminal_);
}

void Logger::periodic_flush()
{
    const auto now = std::chrono::steady_clock::now();
    if (now - last_flush_ < std::chrono::seconds(1))
        return;
    last_flush_ = now;
    flush();
}

void Logger::shutdown()
{
    flush();
}

int Logger::apply_spec(std::string_view spec)
{
    int rejected = 0;
    while (!spec.empty())
    {
        const auto comma = spec.find_first_of(",; ");
        const std::string_view item = trim(spec.substr(0, comma));
        spec = comma == std::string_view::npos ? std::string_view{} : spec.substr(comma + 1);
        if (item.empty())
            continue;
        const auto eq = item.find_first_of("=:");
        if (eq == std::string_view::npos)
        {
            ++rejected;
            continue;
        }
        const std::string_view cat = trim(item.substr(0, eq));
        const int lv = parse_level(item.substr(eq + 1));
        if (lv < 0)
        {
            ++rejected;
            continue;
        }
        if (cat == "*" || iequals(cat, "all"))
        {
            base_level_ = lv;
            level_.fill(static_cast<unsigned char>(lv));
            continue;
        }
        const int c = parse_category(cat);
        if (c < 0)
        {
            ++rejected;
            continue;
        }
        level_[static_cast<std::size_t>(c)] = static_cast<unsigned char>(lv);
    }
    recompute_gates();
    return rejected;
}

bool Logger::set_level(int category, int level)
{
    if (level < 0 || level > static_cast<int>(Level::Trace) || category >= kCategoryCount)
        return false;
    if (category < 0)
    {
        base_level_ = level;
        level_.fill(static_cast<unsigned char>(level));
    }
    else
    {
        level_[static_cast<std::size_t>(category)] = static_cast<unsigned char>(level);
    }
    recompute_gates();
    return true;
}

int Logger::category_level(int category) const
{
    return (category >= 0 && category < kCategoryCount) ? level_[static_cast<std::size_t>(category)] : 0;
}

void Logger::set_console_level(int level)
{
    console_level_ = clamp_level(level);
    recompute_gates();
}

void Logger::recompute_gates()
{
    const int floor = file_ || file_wanted_ ? file_floor_ : 0;
    unsigned char any = 0;
    for (int c = 0; c < kCategoryCount; ++c)
    {
        const unsigned char g = static_cast<unsigned char>(std::max<int>(level_[static_cast<std::size_t>(c)], floor));
        g_ShLogGate[c] = g;
        any = std::max(any, g);
    }
    g_ShLogGateAny = any;
}

std::string Logger::stamp() const
{
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t t = system_clock::to_time_t(now);
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d f%-6u", tm.tm_hour, tm.tm_min, tm.tm_sec,
                  static_cast<int>(ms), frame_.load(std::memory_order_relaxed));
    return buf;
}

std::vector<std::string> Logger::recent_lines(std::size_t max) const
{
    std::lock_guard lock(mutex_);
    const std::size_t skip = recent_.size() > max ? recent_.size() - max : 0;
    return std::vector<std::string>(recent_.begin() + static_cast<std::ptrdiff_t>(skip), recent_.end());
}

void Logger::deliver(int category, int level, std::string_view message, bool echo)
{
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
        message.remove_suffix(1);

    const int cat_level = level_[static_cast<std::size_t>(category)];
    const bool normal   = level <= cat_level;
    const bool to_file  = normal || level <= file_floor_;
    // Console-category lines describe the console itself, which already shows
    // the command and its output.
    const bool to_con = echo || (normal && level <= console_level_ &&
                                 (category != SH_LOGCAT_CONSOLE || level <= SH_LOGLV_WARN));

    char head[64];
    std::snprintf(head, sizeof(head), "%c %-8s", kLevelLetters[level], kCategories[category].name);
    const std::string time_stamp = stamp();

    std::lock_guard lock(mutex_);
    if (normal)
    {
        if (recent_.size() >= kRecentLines)
            recent_.pop_front();
        recent_.push_back(time_stamp + " " + head + " " + std::string(message));
    }
    if (terminal_ && terminal_on_ && normal)
    {
        if (color_)
            std::fprintf(terminal_, "\x1b[2m%s\x1b[0m %s%s %.*s\x1b[0m\n", time_stamp.c_str(), level_color(level),
                         head, static_cast<int>(message.size()), message.data());
        else
            std::fprintf(terminal_, "%s %s %.*s\n", time_stamp.c_str(), head, static_cast<int>(message.size()),
                         message.data());
    }
    if (file_ && to_file)
    {
        std::fprintf(file_, "%s %s %.*s\n", time_stamp.c_str(), head, static_cast<int>(message.size()),
                     message.data());
        if (level == SH_LOGLV_ERROR)
            std::fflush(file_);
    }
    if (to_con)
    {
        std::string text;
        if (echo || category == SH_LOGCAT_GAME)
            text.assign(message);
        else
            text = std::string(kCategories[category].name) + ": " + std::string(message);
        if (console_.size() >= kConsoleQueueMax)
            console_.pop_front();
        console_.push_back({std::move(text), static_cast<unsigned char>(level), echo});
    }
    if (listener_)
    {
        std::string line = std::string(head) + " " + std::string(message);
        listener_(category, level, line.c_str());
    }
}

void Logger::write(int category, int level, const char* fmt, std::va_list ap, bool echo)
{
    if (category < 0 || category >= kCategoryCount)
        category = SH_LOGCAT_GAME;
    level = std::clamp(level, static_cast<int>(Level::Error), static_cast<int>(Level::Trace));
    if (!echo && level > g_ShLogGate[category])
        return;
    const std::string message = vformat(fmt, ap);
    deliver(category, level, message, echo);
}

void Logger::drain_console(ShLogConsoleFn fn)
{
    std::deque<ConsoleLine> lines;
    {
        std::lock_guard lock(mutex_);
        lines.swap(console_);
    }
    for (const ConsoleLine& line : lines)
        fn(line.text.c_str(), line.level, line.toast ? 1 : 0);
}

}  // namespace sh::log

using sh::log::Logger;

extern "C" {

void ShLog_Write(int category, int level, const char* fmt, ...)
{
    std::va_list ap;
    va_start(ap, fmt);
    Logger::instance().write(category, level, fmt, ap, false);
    va_end(ap);
}

void ShLog_VWrite(int category, int level, const char* fmt, va_list ap)
{
    Logger::instance().write(category, level, fmt, ap, false);
}

void ShLog_Echo(const char* fmt, ...)
{
    std::va_list ap;
    va_start(ap, fmt);
    Logger::instance().write(sh::log::category_from_tag(fmt), SH_LOGLV_INFO, fmt, ap, true);
    va_end(ap);
}

int ShLog_CategoryFromText(const char* text)
{
    return text ? sh::log::category_from_tag(text) : SH_LOGCAT_GAME;
}

void ShLog_Init(void)
{
    Logger::instance().init();
}

int ShLog_ConfigKey(const char* key, const char* value)
{
    return (key && value && Logger::instance().config_key(key, value)) ? 1 : 0;
}

void ShLog_SetLegacyDebugLog(int enabled)
{
    Logger::instance().set_legacy_debug_log(enabled != 0);
}

int ShLog_FileWanted(void)
{
    return Logger::instance().file_wanted() ? 1 : 0;
}

int ShLog_StartFile(const char* exeDir)
{
    const std::filesystem::path dir = (exeDir && exeDir[0]) ? std::filesystem::path(exeDir) : std::filesystem::path(".");
    return Logger::instance().start_file(dir) ? 1 : 0;
}

const char* ShLog_FilePath(void)
{
    return Logger::instance().file_path().c_str();
}

void ShLog_SetTerminalStream(FILE* stream)
{
    Logger::instance().set_terminal(stream);
}

void ShLog_SetFrame(unsigned frame)
{
    Logger::instance().set_frame(frame);
}

void ShLog_Flush(void)
{
    Logger::instance().flush();
}

void ShLog_Shutdown(void)
{
    Logger::instance().shutdown();
}

void Sh_LogPeriodicFlush(void)
{
    Logger::instance().periodic_flush();
}

int ShLog_ParseLevel(const char* text)
{
    return text ? sh::log::parse_level(text) : -1;
}

int ShLog_ParseCategory(const char* text)
{
    return text ? sh::log::parse_category(text) : -1;
}

const char* ShLog_LevelName(int level)
{
    return sh::log::level_name(level);
}

const char* ShLog_CategoryName(int category)
{
    return sh::log::category_name(category);
}

const char* ShLog_CategoryHelp(int category)
{
    return sh::log::category_help(category);
}

int ShLog_CategoryLevel(int category)
{
    return Logger::instance().category_level(category);
}

int ShLog_ApplySpec(const char* spec)
{
    return spec ? Logger::instance().apply_spec(spec) : 0;
}

int ShLog_SetLevel(int category, int level)
{
    return Logger::instance().set_level(category, level) ? 1 : 0;
}

int ShLog_ConsoleLevel(void)
{
    return Logger::instance().console_level();
}

void ShLog_SetConsoleLevel(int level)
{
    Logger::instance().set_console_level(level);
}

void ShLog_ConsoleDrain(ShLogConsoleFn fn)
{
    if (fn)
        Logger::instance().drain_console(fn);
}

void ShLog_SetListener(ShLogListenerFn fn)
{
    Logger::instance().set_listener(fn);
}

}  // extern "C"
