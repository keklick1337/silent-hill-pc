// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// New Game difficulties as data: gamedata/config/difficulties.json — the list the
// title's difficulty picker shows, each entry mapping to the base e_GameDifficulty the
// game logic (savegame bitfield, enemy spawn gates, grab-escape rows) understands. The
// shipped file carries the original's three; a mod can replace the list or add entries.

#pragma once

#include "formats/common/status.h"

#include <string>
#include <string_view>
#include <vector>
#include <filesystem>

namespace sh::gconfig {

struct DifficultyEntry {
    std::string label;        // shown in the picker; goes through sh::i18n::menu_text like the built-in strings did
    int base_difficulty = 0;  // e_GameDifficulty (-1 easy .. +1 hard) the game logic uses
    bool is_default = false;  // preselected on the picker (and used by -skiptogame)
    std::string start_map;    // optional map name to boot instead of the manifest's new_game_map
};

struct DifficultyLoadSource {
    std::string mod_id;
};

class DifficultyLibrary {
public:
    // The original's EASY/NORMAL/HARD, so the accessors answer retail behaviour
    // before (and without) the data.
    DifficultyLibrary();

    // <gamedata_dir>/config/difficulties.json; the highest-priority mod's copy replaces it.
    static Result<DifficultyLibrary> load(const std::filesystem::path& gamedata_dir,
                                          const std::filesystem::path& mods_dir,
                                          std::vector<std::string>& warnings, DifficultyLoadSource* source = nullptr);

    static Result<DifficultyLibrary> parse(std::string_view json, std::vector<std::string>& warnings);

    std::size_t count() const { return entries_.size(); }
    const DifficultyEntry& entry(std::size_t index) const { return entries_[index]; }
    // Index of the default entry (the first flagged "default", else the middle one).
    std::size_t default_index() const;

    const std::vector<DifficultyEntry>& entries() const { return entries_; }

    std::vector<std::string> check_consistency() const;

private:
    std::vector<DifficultyEntry> entries_;
};

}  // namespace sh::gconfig
