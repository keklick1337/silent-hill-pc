// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Which files of the disc are movies (gamedata/config/movies.json) and which file replaces one: a mod's or a
// loose one.

#include "formats/common/status.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::fmv {

struct Movie {
    std::string file;           // the disc path, "XA/M1_03500"
    std::string name;           // its last part, the name replacements go by
    std::uint32_t sectors = 0;  // the length of the file on the disc
};

class MovieList {
public:
    static MovieList builtin() { return {}; }
    // {"movies": [{"file": <disc path>, "sectors": <count>, "description": ...}, ...]}
    static Result<MovieList> parse(std::string_view json);

    const std::vector<Movie>& movies() const { return movies_; }
    const Movie* find(std::string_view file) const;

private:
    std::vector<Movie> movies_;
};

// How many frames a movie plays when its caller names no count: the disc's file table gives a movie's size in
// frames, and the game stops seven short of it.
int default_frame_limit(std::uint32_t table_frames);
// The game shows a frame one step after it decodes it and stops when the frame it has just read reaches the
// limit, so the last frame on screen is number limit - 2. True when frame `number` is past that; a limit of 0 or
// less plays to the end of the file.
bool past_frame_limit(std::uint32_t number, int limit);

struct MovieReplacement {
    enum class Kind { None, Avi, Container };
    Kind kind = Kind::None;
    std::string path;  // host path
};

struct ReplacementSources {
    // The host file of the highest mod that has the game path ("FMV/M1_03500.avi").
    std::function<std::optional<std::string>(const std::string& game_path)> mod_file;
    // Whether a loose file exists at a path relative to the working directory.
    std::function<bool(const std::string& path)> loose_file;
    // Off when FFmpeg is not built in: only .avi replaces a movie.
    bool containers = true;
};

// The mods come first, then the loose folders (gamedata/fmv/, ../gamedata/fmv/, the working directory); within
// each an .avi wins over a container (.mp4, .mkv, .webm, .mov, in that order).
MovieReplacement find_replacement(std::string_view name, const ReplacementSources& sources);

}  // namespace sh::fmv
