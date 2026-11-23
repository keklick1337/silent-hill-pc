// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The movie player. A movie is a file of the disc (gamedata/config/movie_files.json lists them) decoded by the
// engine, unless a mod or a loose file replaces it: FMV/<name>.avi of a mod, then the mod's FMV/<name>.mp4 (.mkv,
// .webm, .mov), then the same two in gamedata/fmv/, then the disc. An .avi with
// Motion JPEG or uncompressed pictures and PCM sound plays without anything else; other files go through FFmpeg,
// loaded at run time from the game's folder. A replacement that cannot be played falls back to the disc movie.
//
// Playing blocks the caller until the movie ends or the player skips it (Enter, Escape, Space, the pad's skip).

#include "media/fmv/codec/movie_list.h"

namespace sh::fmv {

// The movie sound level (the options' slider, config fmv_volume and fmv_original_volume).
struct MovieSound {
    float volume = 1.0f;
    // The console's balance of movie sound against the music (pcm.h); off, movies play at the full volume.
    bool console_level = true;
};
MovieSound& movie_sound();

// The movies of the disc, for the console's list.
const MovieList& movie_list();

// Plays disc file number `file` (sh::loader::disc_files()). `frame_limit` is where the game cuts the disc movie;
// 0 takes the movie's own length. False when the file is not a movie or the disc cannot be read.
bool play_movie(int file, int frame_limit);

}  // namespace sh::fmv
