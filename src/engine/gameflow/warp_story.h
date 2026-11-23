// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The story state a debug map warp starts with. A warp is a New Game on
// the picked map, so every story flag is clear; the original never reaches a later map that way, and a one-off
// scene of an earlier chapter can then fire far from its place (leaving the school after the boss with the
// cafe exit still pending runs that scene's camera at the cafe, kilometres from Harry).
//
// The data is an ordered list of chapters, each naming the maps first reached in it and the flags the story has
// set by the time it is over. A warp to a map of chapter k sets the flags of chapters 0..k-1; a map no chapter
// names comes after all of them. gamedata/config/warp_story.json; a mod replaces the file whole.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

namespace sh::gameflow {

struct WarpChapter {
    std::string name;
    std::vector<std::string> maps;        // maps first reached in this chapter
    std::vector<std::string> done_flags;  // flag names (gamedata/flags/) set once the chapter is over
};

class WarpStory {
public:
    // No chapters: a warp starts with every flag clear, as before the data existed.
    static WarpStory builtin() { return {}; }
    static Result<WarpStory> parse(std::string_view json);

    // The flags a warp to `map` starts with, in data order, without repeats.
    std::vector<std::string> flags_for(std::string_view map) const;

    const std::vector<WarpChapter>& chapters() const { return chapters_; }

private:
    std::vector<WarpChapter> chapters_;
};

}  // namespace sh::gameflow
