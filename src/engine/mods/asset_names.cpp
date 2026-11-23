#include "engine/mods/asset_names.h"

#include <cstdio>

namespace sh::mods {

std::string tim_texture_stem(std::string_view disc_path) {
    std::string path(disc_path);
    for (char& c : path) {
        if (c == '\\') {
            c = '/';
        }
    }
    while (!path.empty() && path.front() == '/') {
        path.erase(path.begin());
    }
    const std::size_t slash = path.rfind('/');
    const std::size_t name_at = slash == std::string::npos ? 0 : slash + 1;
    std::size_t end = path.size();
    const std::size_t dot = path.rfind('.');
    if (dot != std::string::npos && dot > name_at) {
        end = dot;
    }
    if (end <= name_at) {
        return {};
    }
    return "convert/textures/" + path.substr(0, end);
}

std::array<std::string, 2> tim_row_names(std::string_view disc_path, int row) {
    if (row < 0 || row >= kMaxTimRows) {
        return {};
    }
    const std::string stem = tim_texture_stem(disc_path);
    if (stem.empty()) {
        return {};
    }
    if (row == 0) {
        return {stem + ".dds", stem + ".png"};
    }
    char suffix[8];
    std::snprintf(suffix, sizeof suffix, ".p%02d", row);
    return {stem + suffix + ".dds", stem + suffix + ".png"};
}

std::optional<ImageFile> find_image(const std::array<std::string, 2>& names, const FindFile& find) {
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i].empty()) {
            continue;
        }
        if (std::optional<std::filesystem::path> hit = find(names[i])) {
            return ImageFile{std::move(*hit), i == 0};
        }
    }
    return std::nullopt;
}

bool tim_is_per_row(std::string_view disc_path, const FindFile& find) {
    for (int row = 1; row < kMaxTimRows; ++row) {
        if (find_image(tim_row_names(disc_path, row), find)) {
            return true;
        }
    }
    return false;
}

TimImages find_tim_images(std::string_view disc_path, const FindFile& find) {
    TimImages out;
    out.per_row = tim_is_per_row(disc_path, find);
    if (!out.per_row) {
        out.whole = find_image(tim_whole_names(disc_path), find);
        return out;
    }
    for (int row = 0; row < kMaxTimRows; ++row) {
        out.rows[static_cast<std::size_t>(row)] = find_image(tim_row_names(disc_path, row), find);
    }
    return out;
}

std::string lower_ascii(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

std::optional<std::filesystem::path> find_dir_nocase(const std::filesystem::path& root, std::string_view relative) {
    std::filesystem::path dir = root;
    std::error_code ec;
    std::size_t at = 0;
    while (at <= relative.size()) {
        std::size_t end = relative.find_first_of("/\\", at);
        if (end == std::string_view::npos) {
            end = relative.size();
        }
        const std::string_view part = relative.substr(at, end - at);
        at = end + 1;
        if (part.empty() || part == ".") {
            continue;
        }
        if (part == "..") {
            return std::nullopt;
        }
        if (std::filesystem::is_directory(dir / part, ec)) {
            dir /= part;
            continue;
        }
        const std::string want = lower_ascii(part);
        std::optional<std::filesystem::path> hit;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.is_directory(ec) && lower_ascii(entry.path().filename().string()) == want) {
                hit = entry.path();
                break;
            }
        }
        if (!hit) {
            return std::nullopt;
        }
        dir = *hit;
    }
    if (!std::filesystem::is_directory(dir, ec)) {
        return std::nullopt;
    }
    return dir;
}

}  // namespace sh::mods
