// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The three ways a movie plays. Private to the movie player.

#include "media/fmv/codec/movie_list.h"

#include <cstdint>
#include <string>

namespace sh::fmv {

// The disc's own movie, from sector `first_sector`. False when the disc cannot be read.
bool play_disc_movie(const Movie& movie, std::uint32_t first_sector, int frame_limit);

// An .avi replacement, to its own end. False when the file is no AVI or its picture codec is not one of the
// built-in ones; nothing was shown then.
bool play_avi_movie(const std::string& path);

// Any file FFmpeg reads, to its own end. False when FFmpeg is not there or shows no picture of the file.
bool play_container_movie(const std::string& path);
// Whether this build can try play_container_movie at all.
bool container_movies_built();

}  // namespace sh::fmv
