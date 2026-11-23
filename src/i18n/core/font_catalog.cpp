// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n/core/font_catalog.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace sh::i18n {
namespace {

namespace fs = std::filesystem;

constexpr int kTargetWeight = 700;  // the game's letters are bold
constexpr int kInUseBonus = 25;

#ifdef _WIN32
constexpr char kListSeparator = ';';
#else
constexpr char kListSeparator = ':';
#endif

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool has_word(const std::string& text, std::string_view word) { return text.find(word) != std::string::npos; }

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr ? v : "";
}

void split_list(const std::string& list, std::vector<fs::path>& out, const char* append = nullptr) {
    std::size_t start = 0;
    while (start <= list.size()) {
        const std::size_t end = std::min(list.find(kListSeparator, start), list.size());
        if (end > start) {
            fs::path p(list.substr(start, end - start));
            out.push_back(append != nullptr ? p / append : p);
        }
        start = end + 1;
    }
}

bool is_font_file(const fs::path& p) {
    const std::string ext = lower(p.extension().string());
    return ext == ".ttf" || ext == ".otf" || ext == ".ttc" || ext == ".otc";
}

struct Keyword {
    std::string_view word;
    int score;
};

// Words of a family or style name and what they say about the face.
constexpr Keyword kKeywords[] = {
    {"sans", 40},      {"gothic", 25},     {"kufi", 20},       {"hei", 15},       {"arial", 50},
    {"helvetica", 45}, {"segoe ui", 50},   {"tahoma", 45},     {"verdana", 40},   {"dejavu", 10},
    {"noto", 20},      {"liberation", 5},  {"yahei", 50},      {"meiryo", 50},    {"malgun", 50},
    {"pingfang", 50},  {"hiragino", 40},   {"nanum", 10},      {"cjk", 10},
    {"mono", -60},     {"typewriter", -60}, {"courier", -60},  {"console", -60},  {"fixed", -60},
    {"condensed", -30}, {"narrow", -30},   {"compressed", -30}, {"extended", -20},
    {"display", -150}, {"decor", -150},    {"script", -150},   {"hand", -150},    {"brush", -150},
    {"comic", -150},   {"symbol", -200},   {"emoji", -200},    {"math", -200},    {"music", -200},
    {"icon", -200},    {"dingbat", -200},  {"awesome", -200},  {"nerd", -200},    {"braille", -200},
    {"sign", -150},    {"outline", -150},  {"shadow", -150},   {"stencil", -150}, {"fraktur", -150},
    {"nastaliq", -40}, {"rashi", -60},
};

}  // namespace

std::vector<fs::path> system_font_directories() {
    std::vector<fs::path> dirs;
    if (std::getenv("SH_FONT_DIRS") != nullptr) {
        split_list(env("SH_FONT_DIRS"), dirs);
        return dirs;
    }
#ifdef _WIN32
    const std::string windir = env("WINDIR").empty() ? env("SystemRoot") : env("WINDIR");
    dirs.emplace_back(fs::path(windir.empty() ? "C:\\Windows" : windir) / "Fonts");
    if (!env("LOCALAPPDATA").empty()) {
        dirs.emplace_back(fs::path(env("LOCALAPPDATA")) / "Microsoft" / "Windows" / "Fonts");
    }
#else
    const std::string home = env("HOME");
#ifdef __APPLE__
    if (!home.empty()) {
        dirs.emplace_back(fs::path(home) / "Library" / "Fonts");
    }
    dirs.emplace_back("/Library/Fonts");
    dirs.emplace_back("/System/Library/Fonts");
#endif
    // The directories fontconfig's stock configuration names: the XDG data directories and ~/.fonts.
    if (!env("XDG_DATA_HOME").empty()) {
        dirs.emplace_back(fs::path(env("XDG_DATA_HOME")) / "fonts");
    } else if (!home.empty()) {
        dirs.emplace_back(fs::path(home) / ".local" / "share" / "fonts");
    }
    if (!home.empty()) {
        dirs.emplace_back(fs::path(home) / ".fonts");
    }
    split_list(env("XDG_DATA_DIRS").empty() ? "/usr/local/share:/usr/share" : env("XDG_DATA_DIRS"), dirs, "fonts");
    dirs.emplace_back("/usr/share/fonts");
    dirs.emplace_back("/usr/local/share/fonts");
    dirs.emplace_back("/usr/X11R6/lib/X11/fonts");
#endif
    // A directory named twice is walked once.
    std::vector<fs::path> unique;
    for (const fs::path& d : dirs) {
        const fs::path n = d.lexically_normal();
        if (std::find(unique.begin(), unique.end(), n) == unique.end()) {
            unique.push_back(n);
        }
    }
    return unique;
}

int font_style_score(const FontFace& face) {
    const std::string name = lower(face.family + " " + face.style);
    int score = -std::abs(face.weight - kTargetWeight) / 10;
    if (face.italic || has_word(name, "italic") || has_word(name, "oblique")) {
        score -= 200;
    }
    if (face.monospace) {
        score -= 60;
    }
    if (has_word(name, "serif") && !has_word(name, "sans")) {
        score -= 40;
    }
    for (const Keyword& k : kKeywords) {
        if (has_word(name, k.word)) {
            score += k.score;
        }
    }
    return score;
}

void FontCatalog::add_face(FontFace face, FontTier tier) {
    Entry e;
    e.score = font_style_score(face);
    e.face = std::move(face);
    e.tier = tier;
    entries_.push_back(std::move(e));
}

std::size_t FontCatalog::add_file(const fs::path& file, FontTier tier) {
    std::vector<FontFace> faces = read_font_faces(file);
    for (FontFace& f : faces) {
        add_face(std::move(f), tier);
    }
    return faces.size();
}

std::size_t FontCatalog::add_directory(const fs::path& dir, FontTier tier) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return 0;
    }
    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied |
                                                      fs::directory_options::follow_directory_symlink, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (it.depth() > 8) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec) && is_font_file(it->path())) {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    std::size_t added = 0;
    for (const fs::path& f : files) {
        added += add_file(f, tier);
    }
    return added;
}

const FontFace* FontCatalog::choose(char32_t cp, std::span<const std::string> preferred,
                                    std::span<const FontFace* const> in_use, FontRank* why,
                                    std::span<const FontFace* const> excluded) const {
    const Entry* best = nullptr;
    long best_rank = 0;
    std::vector<std::string> wanted;
    wanted.reserve(preferred.size());
    for (const std::string& p : preferred) {
        wanted.push_back(lower(p));
    }
    for (const Entry& e : entries_) {
        if (!e.face.has(cp) || std::find(excluded.begin(), excluded.end(), &e.face) != excluded.end()) {
            continue;
        }
        // One number orders everything: the class of the face first, the style score inside a class. Faces of a
        // class that is taken in order (mods, game data) all score alike, so the first one stays.
        long rank = 0;
        const std::string family = lower(e.face.family);
        const auto named = std::find(wanted.begin(), wanted.end(), family);
        if (e.tier == FontTier::Mod) {
            rank = 4'000'000;
        } else if (named != wanted.end()) {
            // An upright face of the family over its italics, but any face of an earlier family over a later one.
            rank = 3'000'000 - static_cast<long>(named - wanted.begin()) * 10'000 + std::clamp(e.score, -4000, 4000);
        } else if (e.tier == FontTier::GameData) {
            rank = 2'000'000;
        } else {
            rank = 1'000'000 + std::clamp(e.score, -4000, 4000);
            if (std::find(in_use.begin(), in_use.end(), &e.face) != in_use.end()) {
                rank += kInUseBonus;
            }
        }
        if (best == nullptr || rank > best_rank) {
            best = &e;
            best_rank = rank;
        }
    }
    if (best != nullptr && why != nullptr) {
        *why = best_rank >= 4'000'000   ? FontRank::Mod
               : best_rank > 2'000'000  ? FontRank::Preferred
               : best_rank >= 2'000'000 ? FontRank::GameData
                                        : FontRank::System;
    }
    return best != nullptr ? &best->face : nullptr;
}

}  // namespace sh::i18n
