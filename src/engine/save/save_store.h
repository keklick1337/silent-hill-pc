// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#pragma once

// The save directory: <exe>/.savegames/ unless savegame_dir says otherwise.
//
//   save_NNNN.sav            one save each, as many as the player makes; NNNN grows with every new save
//   quick/quick_NN.sav       quick saves
//   import/                  memory card images to import (memory_card.h, import_drop_folder)
//   migration.json           what was already imported (card images, the old card1/card2 layout)
//   card1/ card2/            the layout of older builds (two memory cards of 15 file groups): read once by
//                            migrate_card_layout, left in place
//
// Every write goes through write_file_atomic. Nothing is cached: the files are the state, so a save copied in by
// hand shows up the next time the game lists the saves.

#include "engine/save/save_file.h"
#include "engine/save/save_game.h"
#include "engine/save/saved_mods.h"
#include "formats/common/status.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sh::save {

inline constexpr int kQuickSlots = 10;

// The old layout (two memory cards, 15 file groups of 11 saves each), kept for the migration.
inline constexpr int kLegacyCardCount = 2;
inline constexpr int kLegacyFilesPerCard = 15;
inline constexpr int kLegacySlotsPerFile = 11;

// Descriptive fields for the META chunk (people, tools, mods, the save list of the screen).
struct SlotDescription {
    int map_index = -1;
    std::string map_name;
    int location_id = -1;
    std::string location_name;
    int difficulty = -1;
    std::string difficulty_name;
    double play_time_seconds = 0;
    bool next_fear = false;
    int save_count = 0;
    // The mods the save was made with; none: the save does not say (it keeps the "mods" key out of META).
    std::optional<std::vector<SavedMod>> mods;
};

struct SlotRecord {
    SlotSummary summary;
    // The STAT chunk: the save game as JSON (save_game.h, to_json). A read fills it for files of both formats.
    std::string state;
    // Format 1 files and memory cards: the game's records as they were stored (GAME, NAME, OPTS). A write with no
    // `state` turns them into one (SaveStore::state_of); the records themselves are never written again.
    std::vector<std::uint8_t> legacy_game;
    std::vector<std::uint8_t> legacy_options;
    std::string legacy_names;
    // The RMNS chunk as stored (empty: none).
    std::string remains;
    // META "mods" (META format 3): the mods the save was made with, written as they are on every rewrite. None: a
    // save of an older build, which does not say.
    std::optional<std::vector<SavedMod>> mods;
};

enum class SlotState { Empty, Ok, Damaged };

struct SlotRead {
    SlotState state = SlotState::Empty;
    SlotRecord record;  // Ok: the record; Damaged: what could be recovered (may be empty)
    std::string error;  // Damaged: why
    Warnings warnings;  // Ok: what a format 1 file lost on the way (names this build does not know)
};

// One line of the save list: INFO and META of a save file, without its game state.
struct SaveEntry {
    int number = 0;
    SlotState state = SlotState::Empty;
    SlotSummary summary;
    // From META; -1 / empty / 0 when the file has none (a save written by hand or by an older tool).
    int map_index = -1;
    int location_id = -1;
    int difficulty = 0;  // the game's value (-1 Easy, 0 Normal, 1 Hard) when has_difficulty
    bool has_difficulty = false;
    std::string location_name;
    std::string difficulty_name;
    std::int64_t saved_at = 0;  // UTC, seconds since 1970; 0 when unknown
    double play_time_seconds = 0;
    std::optional<std::vector<SavedMod>> mods;  // as SlotRecord::mods
    std::string error;  // Damaged: why
};

class SaveStore {
public:
    explicit SaveStore(std::filesystem::path root);

    const std::filesystem::path& root() const { return root_; }
    std::filesystem::path save_path(int number) const;
    std::filesystem::path quick_path(int index) const;

    // Numbers of the save files on disk, ascending.
    std::vector<int> numbers() const;
    // The number a new save gets: one past the highest on disk, so numbers never come back after a delete.
    int next_number() const;
    // Every save, newest first (highest sequence; equal sequences: highest number). Damaged files are listed too.
    std::vector<SaveEntry> list() const;
    // Highest sequence of the saves (0 when there are none); a new save gets one more.
    std::int32_t newest_sequence() const;
    // A save with the same summary and game state is already in the list (imports and migration skip it).
    bool contains(const SlotRecord& record) const;

    SlotRead read(int number) const;
    // number <= 0 writes a new save (next_number); otherwise replaces that save. Returns the number written. The
    // META chunk is described from the state.
    Result<int> write(int number, const SlotRecord& record);
    Result<void> remove(int number);

    SlotRead read_quick(int index) const;
    Result<void> write_quick(int index, const SlotRecord& record);

    // A save file outside the store's folders (a state dump's state.sav): the quick save layout at `path`.
    SlotRead read_at(const std::filesystem::path& path) const;
    Result<void> write_at(const std::filesystem::path& path, const SlotRecord& record) const;

    // The old two-card layout (0-based card, file, slot; file names 1-based).
    std::filesystem::path legacy_card_dir(int card) const;
    std::filesystem::path legacy_group_path(int card, int file) const;
    std::filesystem::path legacy_slot_path(int card, int file, int slot) const;
    // A slot of the old layout, with the options of its group in record.options.
    SlotRead read_legacy_slot(int card, int file, int slot) const;

    // The names of this build's things (the game installs them). Without them every thing of a converted save
    // stays a number.
    void set_names(NameTable names) { names_ = std::move(names); }
    const NameTable& names() const { return names_; }

    // The STAT text of a record: its state, or its format 1 records converted (fails when they are damaged).
    Result<std::string> state_of(const SlotRecord& record, Warnings& warnings) const;
    // What META says about a state.
    SlotDescription describe_state(const SaveGame& game) const;

    // DEPRECATED, to be removed in a future version (with the readers of the old formats): rewrites every save and
    // quick save of an older format (file version < kFormatVersion or a STAT chunk of kSaveGameFormatV1) in the
    // current one. Each file is first copied to <root>/old_format_backup/ (never overwriting a copy there); the new
    // file must give the same game state as the old one, else the old file is put back. A file already in the
    // current format is left alone. One line per file in `lines`; returns the number converted.
    int convert_old_saves(std::vector<std::string>& lines);

private:
    SlotRead read_file(const std::filesystem::path& path, FileKind kind) const;
    Result<void> write_file(const std::filesystem::path& path, FileKind kind, const SlotRecord& record, int number,
                            int quick_index) const;

    std::filesystem::path root_;
    NameTable names_;
};

// The META JSON of a save (number > 0) or a quick save (quick_index >= 0).
std::string describe(const SlotDescription& description, int number, int quick_index);

// The mods of a META JSON: its "mods" list when the META is of format 3 or later (an older META's "mods" was always
// empty and says nothing); nothing otherwise.
std::optional<std::vector<SavedMod>> mods_of_meta(std::string_view meta_json);

// "2026-11-06T14:30:00Z" -> seconds since 1970 (UTC); 0 when it does not parse.
std::int64_t parse_utc(const std::string& text);

}  // namespace sh::save
