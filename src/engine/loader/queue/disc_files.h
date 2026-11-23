// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The files of the disc by name: the imported file table
// (gamedata/tables/filetable.json) as paths like "CHARA/HERO.ILM" with their first sector and size.
// A file's number is its position in the table; it is only a handle for the queue and is never stored:
// code and data name files by path, and find() gives the number of the moment.

#pragma once

#include "formats/convert/filetable/file_table.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sh::loader {

class DiscFiles {
public:
    static constexpr int kNone = -1;
    static constexpr std::uint32_t kBlockBytes = 256;
    static constexpr std::uint32_t kSectorBytes = 2048;
    static constexpr std::size_t kRawEntryBytes = 12;  // one entry as a boot executable stores it

    // Replaces the whole table; position in `records` is the file number.
    void assign(std::vector<convert::filetable::Record> records);
    bool loaded() const { return !files_.empty(); }
    int count() const { return static_cast<int>(files_.size()); }
    bool valid(int file) const { return file >= 0 && file < count(); }

    // "CHARA/HERO.ILM", any case, '\' or '/', with or without a leading separator; kNone when absent.
    int find(std::string_view path) const;
    // The file `offset` places after `first_path`, or kNone when `first_path` is absent: the game numbers some
    // runs of files from their first one (map overlays "VIN/MAP0_S00.BIN" + map, Harry's map animations
    // "ANIM/HB_M0S00.ANM" + map, the paper maps "TIM/MP_0TOWN.TIM" + page).
    int find_in_run(std::string_view first_path, int offset) const {
        const int first = find(first_path);
        return first == kNone ? kNone : first + offset;
    }

    // Disc path ("XA/Z1_16180" has no extension), "" for an invalid number.
    const std::string& path(int file) const;
    std::string name(int file) const;       // "HERO.ILM"
    std::string directory(int file) const;  // "CHARA"
    std::uint32_t first_sector(int file) const;
    std::uint32_t bytes(int file) const;           // the table size: 256-byte blocks
    std::uint32_t sector_bytes(int file) const;    // bytes() rounded up to whole sectors
    int type(int file) const;                      // extension index, 15 = none
    // The entry as the table stores it (name in 6-bit characters); an empty record for an invalid number.
    const convert::filetable::Record& record(int file) const;

    // The game's own searches. next_of_type walks from start+step (step by the sign of `direction`),
    // wrapping at both ends, and gives up after one lap. next_named matches the first 8 characters of
    // `name` before a '.' as the table stores them (6 bits each) and the type, from `start` forward.
    int next_of_type(int type, int start, int direction) const;
    int next_named(const char* name, int type, int start) const;

    // A rebuilt disc keeps every name but moves files: take each file's sector and size from the
    // disc's own table, matched on name, type and directory. Returns how many files moved. The
    // sectors before the last remap stay known, for tables that point at files by sector.
    int remap(std::span<const std::uint8_t> raw_entries);
    // The sector a file starting at `old_sector` before the last remap starts at now; `old_sector`
    // itself when no file started there.
    std::uint32_t moved_sector(std::uint32_t old_sector) const;

    static convert::filetable::Record unpack(const std::uint8_t* raw);

private:
    struct Entry {
        convert::filetable::Record record;
        std::string path;
    };

    static std::string key_of(std::string_view path);
    void index_paths();

    std::vector<Entry> files_;
    std::unordered_map<std::string, int> by_path_;
    std::vector<std::uint32_t> sectors_before_remap_;
};

// The table of the running game.
DiscFiles& disc_files();

}  // namespace sh::loader
