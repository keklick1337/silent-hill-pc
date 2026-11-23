// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/fmv/codec/movie_list.h"

#include "formats/common/json.h"

#include <array>
#include <utility>

namespace sh::fmv {

namespace {

constexpr std::array<std::string_view, 2> kAviExtensions = {".avi", ".AVI"};
constexpr std::array<std::string_view, 8> kContainerExtensions = {".mp4", ".MP4", ".mkv", ".MKV",
                                                                  ".webm", ".WEBM", ".mov", ".MOV"};
constexpr std::array<std::string_view, 3> kLooseFolders = {"gamedata/fmv/", "../gamedata/fmv/", ""};

template <std::size_t N>
std::optional<std::string> first_found(std::string_view folder, std::string_view name,
                                       const std::array<std::string_view, N>& extensions,
                                       const std::function<std::optional<std::string>(const std::string&)>& probe) {
    for (std::string_view extension : extensions) {
        std::string path(folder);
        path += name;
        path += extension;
        if (std::optional<std::string> found = probe(path)) return found;
    }
    return std::nullopt;
}

}  // namespace

Result<MovieList> MovieList::parse(std::string_view text) {
    using R = Result<MovieList>;
    const Result<json::Value> doc = json::parse(text);
    if (!doc) return R::fail("movies.json: " + doc.error());
    const json::Value* list = doc.value().is_object() ? doc.value().find("movies") : nullptr;
    if (list == nullptr || !list->is_array()) return R::fail("movies.json: \"movies\" must be an array");
    MovieList out;
    for (const json::Value& entry : list->array()) {
        const json::Value* file = entry.is_object() ? entry.find("file") : nullptr;
        const json::Value* sectors = entry.is_object() ? entry.find("sectors") : nullptr;
        if (file == nullptr || !file->is_string() || file->string().empty() || sectors == nullptr ||
            !sectors->is_integer() || sectors->integer() <= 0 || sectors->integer() > 0x7FFFFFFF) {
            return R::fail("movies.json: a movie is {\"file\": <disc path>, \"sectors\": <count>}");
        }
        Movie movie;
        movie.file = file->string();
        const std::size_t slash = movie.file.find_last_of('/');
        movie.name = slash == std::string::npos ? movie.file : movie.file.substr(slash + 1);
        movie.sectors = static_cast<std::uint32_t>(sectors->integer());
        if (movie.name.empty()) return R::fail("movies.json: \"" + movie.file + "\" names no file");
        out.movies_.push_back(std::move(movie));
    }
    return R::ok(std::move(out));
}

const Movie* MovieList::find(std::string_view file) const {
    for (const Movie& movie : movies_) {
        if (movie.file == file) return &movie;
    }
    return nullptr;
}

int default_frame_limit(std::uint32_t table_frames) { return table_frames > 7 ? static_cast<int>(table_frames - 7) : 0; }

bool past_frame_limit(std::uint32_t number, int limit) {
    return limit > 0 && number + 2 > static_cast<std::uint32_t>(limit);
}

MovieReplacement find_replacement(std::string_view name, const ReplacementSources& sources) {
    using Kind = MovieReplacement::Kind;
    const auto in_mods = [&](const std::string& path) -> std::optional<std::string> {
        return sources.mod_file ? sources.mod_file(path) : std::nullopt;
    };
    const auto loose = [&](const std::string& path) -> std::optional<std::string> {
        return sources.loose_file && sources.loose_file(path) ? std::optional<std::string>(path) : std::nullopt;
    };

    if (auto path = first_found("FMV/", name, kAviExtensions, in_mods)) return {Kind::Avi, std::move(*path)};
    if (sources.containers) {
        if (auto path = first_found("FMV/", name, kContainerExtensions, in_mods)) return {Kind::Container, std::move(*path)};
    }
    for (std::string_view folder : kLooseFolders) {
        if (auto path = first_found(folder, name, kAviExtensions, loose)) return {Kind::Avi, std::move(*path)};
    }
    if (sources.containers) {
        for (std::string_view folder : kLooseFolders) {
            if (auto path = first_found(folder, name, kContainerExtensions, loose)) return {Kind::Container, std::move(*path)};
        }
    }
    return {};
}

}  // namespace sh::fmv
