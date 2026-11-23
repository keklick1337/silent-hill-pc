#pragma once

// The game's file table: the index of every file in the SILENT. and HILL. archives (name, directory, type, first
// CD sector, size in 256-byte blocks). It lives in the boot executable; the import writes it as
// gamedata/tables/filetable.json and the game reads it from there.

#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::convert::filetable {

// One file. Position in Table::files is the game's file index.
struct Record {
    std::uint32_t sector = 0;   // absolute CD sector of the first byte, 19 bits
    std::uint32_t blocks = 0;   // size in 256-byte units, 12 bits
    std::uint8_t dir = 0;       // index into the directory list (0 = 1ST ... 10 = XA)
    std::uint8_t type = 0;      // index into the extension list; 15 = no extension
    std::uint32_t name0123 = 0; // first four characters, 6 bits each (ASCII - 0x20), 24 bits
    std::uint32_t name4567 = 0; // last four characters
};

struct Table {
    std::string release;        // "SLUS-00707"
    std::uint32_t exe_offset = 0;
    std::vector<Record> files;
};

inline constexpr const char* kRelativePath = "tables/filetable.json";
inline constexpr const char* kRawExe = "raw/SLUS_007.07";
inline constexpr std::uint8_t kNoType = 15;
inline constexpr std::size_t kRecordBytes = 12;

// Directory and extension names, without separators or dot.
std::span<const char* const> directories();
std::span<const char* const> extensions();

// The table of a released executable, recognised by the CRC32 of its first 4096 bytes: NTSC 1.1 is read,
// any other executable is refused.
Result<Table> read_from_exe(std::span<const std::uint8_t> exe);

// The 12 bytes of the record as the executable stores them.
std::array<std::uint8_t, kRecordBytes> pack(const Record& record);

// "CHARA/HERO.ILM"; XA tracks have no extension: "XA/Z1_16180".
std::string path_of(const Record& record);

// The inverse of path_of.
Result<Record> record_from(std::string_view path, std::uint32_t sector, std::uint32_t blocks);

// gamedata/tables/filetable.json text (version 1) and back. from_json is strict: no field may be missing.
std::string to_json(const Table& table);
Result<Table> from_json(std::string_view text);

// The table from <gamedata>/tables/filetable.json. When that file is absent it is built from
// <gamedata>/raw/SLUS_007.07, checked against the executable's bytes and written first. When neither exists, or
// either is broken, this is an error: there is no other copy of the table.
Result<Table> load_or_import(const std::filesystem::path& gamedata_dir);

// The import step: writes tables/filetable.json from raw/SLUS_007.07, verified byte for byte. An existing file is
// kept unless `overwrite`. The value is true when the file was written.
Result<bool> write_from_raw(const std::filesystem::path& gamedata_dir, bool overwrite);

}  // namespace sh::convert::filetable
