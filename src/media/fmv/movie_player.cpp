// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/fmv/movie_player.h"

#include "media/fmv/movie_sources.h"

#include "engine/app/pc_paths.h"
#include "engine/debug/dbg_trace.h"
#include "engine/gameflow/flow_data.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/log/sh_log.h"
#include "engine/timing/vblank.h"
#include "engine/vfs/vfs_c.h"

#include <cstdio>

// The folder of the game data in use (engine/app/game_data_dir.h).
extern "C" const char* PcPort_GetGameDataPath(void);

namespace sh::fmv {

namespace {

// The journal's bracket around a movie, closed on every way out.
class JournalEntry {
public:
    JournalEntry(const Movie& movie, int file, int frame_limit) { DbgTrace_FmvBegin(movie.name.c_str(), file, frame_limit); }
    ~JournalEntry() { DbgTrace_FmvEnd(0); }
    JournalEntry(const JournalEntry&) = delete;
    JournalEntry& operator=(const JournalEntry&) = delete;
};

ReplacementSources replacement_sources() {
    ReplacementSources sources;
    sources.mod_file = [](const std::string& game_path) -> std::optional<std::string> {
        char host_path[1024];
        if (ShVfs_ModFilePath(game_path.c_str(), host_path, sizeof(host_path)) == 0) return std::nullopt;
        return std::string(host_path);
    };
    sources.loose_file = [](const std::string& path) {
        std::FILE* file = std::fopen(path.c_str(), "rb");
        if (file == nullptr) return false;
        std::fclose(file);
        return true;
    };
    sources.containers = container_movies_built();
    return sources;
}

}  // namespace

MovieSound& movie_sound() {
    static MovieSound sound;
    return sound;
}

const MovieList& movie_list() {
    static const MovieList list = [] {
        char mods[1024];
        PcPaths_ExeFile(mods, sizeof(mods), "mods");
        std::string error;
        MovieList loaded = gameflow::load_config<MovieList>(PcPort_GetGameDataPath(), mods, "movie_files.json", &error);
        if (!error.empty()) SH_ERR("[FMV] config/%s", error.c_str());
        if (loaded.movies().empty()) SH_ERR("[FMV] config/movie_files.json lists no movie: none will play");
        return loaded;
    }();
    return list;
}

bool play_movie(int file, int frame_limit) {
    const loader::DiscFiles& files = loader::disc_files();
    const Movie* movie = movie_list().find(files.path(file));
    if (movie == nullptr) {
        SH_LOG_DEBUG(FMV, "[FMV] disc file %d (%s) is not a movie", file, files.path(file).c_str());
        return false;
    }
    const int limit = frame_limit > 0 ? frame_limit : default_frame_limit(files.record(file).blocks);

    // A replay run shows no picture; the movie's time passes: 15 frames a second, four
    // blanks a frame.
    if (timing::virtual_time::active()) {
        SH_LOG_DEBUG(FMV, "[FMV] replay mode: %s is not shown, %d frames pass", movie->name.c_str(), limit);
        for (int blank = 0; blank < limit * 4; ++blank) timing::virtual_time::step_blank();
        return true;
    }

    const JournalEntry journal(*movie, file, frame_limit);
    const std::uint32_t first_sector = files.first_sector(file);
    const MovieReplacement replacement = find_replacement(movie->name, replacement_sources());
    switch (replacement.kind) {
        case MovieReplacement::Kind::None:
            SH_LOG_DEBUG(FMV, "[FMV] %s: the disc movie", movie->name.c_str());
            break;
        case MovieReplacement::Kind::Avi:
            SH_LOG_DEBUG(FMV, "[FMV] %s: replaced by %s", movie->name.c_str(), replacement.path.c_str());
            if (play_avi_movie(replacement.path)) return true;
            // An .avi the built-in reader does not take (H.264 inside, or another format under that name) may
            // still be one FFmpeg reads.
            if (container_movies_built() && play_container_movie(replacement.path)) return true;
            SH_LOG_DEBUG(FMV, "[FMV] %s could not be played: the disc movie instead", replacement.path.c_str());
            break;
        case MovieReplacement::Kind::Container:
            SH_LOG_DEBUG(FMV, "[FMV] %s: replaced by %s", movie->name.c_str(), replacement.path.c_str());
            if (play_container_movie(replacement.path)) return true;
            SH_LOG_DEBUG(FMV, "[FMV] %s could not be played: the disc movie instead", replacement.path.c_str());
            break;
    }
    return play_disc_movie(*movie, first_sector, limit);
}

}  // namespace sh::fmv
