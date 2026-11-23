#include "import/ui/input_check.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <system_error>

namespace sh::import::ui {

namespace fs = std::filesystem;

namespace {

constexpr std::uint64_t kRawSector = 2352;
constexpr std::uint64_t kCookedSector = 2048;
constexpr std::uint64_t kXaSector = 2336;

constexpr std::array<unsigned char, 12> kSync = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

// Name from the first FILE line of a cue sheet; quotes group a name with spaces.
std::optional<std::string> cue_file_name(std::istream& in) {
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (first && line.rfind("\xEF\xBB\xBF", 0) == 0) {
            line.erase(0, 3);
        }
        first = false;
        std::string_view rest = trim(line);
        if (rest.size() < 5 || lower(std::string(rest.substr(0, 4))) != "file" ||
            !std::isspace(static_cast<unsigned char>(rest[4]))) {
            continue;
        }
        rest = trim(rest.substr(4));
        if (rest.empty()) {
            return std::nullopt;
        }
        if (rest.front() == '"') {
            const std::size_t end = rest.find('"', 1);
            if (end == std::string_view::npos) {
                return std::nullopt;
            }
            return std::string(rest.substr(1, end - 1));
        }
        const std::size_t end = rest.find_first_of(" \t");
        return std::string(rest.substr(0, end));
    }
    return std::nullopt;
}

// `bin` must exist: the caller has checked that.
void check_sectors(ImageCheck& check) {
    std::error_code ec;
    if (fs::is_directory(check.bin, ec)) {
        check.problem = Problem::IsDirectory;
        return;
    }
    check.size = fs::file_size(check.bin, ec);
    if (ec) {
        check.problem = Problem::Unreadable;
        return;
    }
    if (check.size == 0) {
        check.problem = Problem::EmptyFile;
        return;
    }

    std::array<unsigned char, 12> head{};
    std::size_t got = 0;
    {
        std::ifstream in(check.bin, std::ios::binary);
        if (!in) {
            check.problem = Problem::Unreadable;
            return;
        }
        in.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
        got = static_cast<std::size_t>(in.gcount());
    }
    const bool sync = got == head.size() && head == kSync;

    // A size that fits both 2048 and 2352 is decided by the sync pattern alone.
    if (sync) {
        check.problem = check.size % kRawSector == 0 ? Problem::None : Problem::BadSectorSize;
    } else if (check.size % kCookedSector == 0) {
        check.problem = Problem::CookedIso;
    } else if (check.size % kXaSector == 0) {
        check.problem = Problem::BadSectorSize;
    } else {
        check.problem = Problem::NotRawImage;
    }
}

}  // namespace

fs::path normalize_path_text(std::string_view text) {
    std::string_view t = trim(text);
    if (t.size() >= 2 && (t.front() == '"' || t.front() == '\'') && t.back() == t.front()) {
        t = trim(t.substr(1, t.size() - 2));
    }
    if (t.empty()) {
        return {};
    }
    std::string s(t);
    if (s == "~" || s.rfind("~/", 0) == 0) {
        if (const char* home = std::getenv("HOME"); home && *home) {
            s = std::string(home) + s.substr(1);
        }
    }
    std::error_code ec;
    fs::path p = fs::absolute(fs::path(s), ec);
    return ec ? fs::path(s) : p.lexically_normal();
}

ImageCheck check_image(std::string_view text) {
    ImageCheck check;
    check.path = normalize_path_text(text);
    if (check.path.empty()) {
        check.problem = Problem::Empty;
        return check;
    }

    std::error_code ec;
    const fs::file_status st = fs::status(check.path, ec);
    if (st.type() == fs::file_type::not_found || (ec && ec == std::errc::no_such_file_or_directory)) {
        check.problem = Problem::NotFound;
        return check;
    }
    if (ec) {
        check.problem = Problem::Unreadable;
        return check;
    }
    if (st.type() == fs::file_type::directory) {
        check.problem = Problem::IsDirectory;
        return check;
    }
    if (st.type() != fs::file_type::regular) {
        check.problem = Problem::Unreadable;
        return check;
    }

    check.bin = check.path;
    if (lower(check.path.extension().string()) == ".cue") {
        std::ifstream in(check.path, std::ios::binary);
        if (!in) {
            check.problem = Problem::Unreadable;
            return check;
        }
        const std::optional<std::string> name = cue_file_name(in);
        if (!name || name->empty()) {
            check.problem = Problem::CueNoFile;
            return check;
        }
        check.detail = *name;
        check.bin = check.path.parent_path() / fs::path(*name);
        if (!fs::is_regular_file(check.bin, ec)) {
            check.problem = Problem::CueTargetMissing;
            return check;
        }
    }
    check_sectors(check);
    return check;
}

std::string describe(Lang lang, const ImageCheck& check) {
    const std::string path = check.path.string();
    switch (check.problem) {
    case Problem::None:
        return {};
    case Problem::Empty:
        return format(lang, Str::ErrEmpty);
    case Problem::NotFound:
        return format(lang, Str::ErrNotFound, {path});
    case Problem::IsDirectory:
        return format(lang, Str::ErrIsDirectory, {path});
    case Problem::Unreadable:
        return format(lang, Str::ErrUnreadable, {check.bin.empty() ? path : check.bin.string()});
    case Problem::CueNoFile:
        return format(lang, Str::ErrCueNoFile, {path});
    case Problem::CueTargetMissing:
        return format(lang, Str::ErrCueTargetMissing, {check.detail});
    case Problem::EmptyFile:
        return format(lang, Str::ErrEmptyFile, {check.bin.string()});
    case Problem::CookedIso:
        return format(lang, Str::ErrCookedIso);
    case Problem::BadSectorSize:
        return format(lang, Str::ErrBadSectorSize, {std::to_string(check.size)});
    case Problem::NotRawImage:
        return format(lang, Str::ErrNotRaw);
    }
    return {};
}

}  // namespace sh::import::ui
