#pragma once

// Animation tables in gamedata/tables/anim/<name>.json.
//
// File format, version 1. Unknown keys are ignored; "index", "speed", "source", "copies" and "help" only help the
// reader and are checked when present.
//
//   {
//     "format": "sh-anim-table", "version": 1, "symbol": "CAT_ANIM_INFOS",
//     "source": {"file": "VIN/MAP1_S01.BIN", "offset": "0x133AC", "va": "0x800DC924", "stride": 16},
//     "entries": [
//       {"index": 2, "kind": "blend_linear", "status": 2, "variable_duration": false, "link": 3,
//        "speed": 64, "speed_q12": 262144, "start_kf": -1, "end_kf": 7},
//       ...
//     ]
//   }
//
// Entry i is the table entry for the packed anim status i. Fields:
//   kind               "blend_linear", "playback_once", "playback_loop" or "none" (no playback function)
//   status             0..255, (anim << 1) | active; 255 = none
//   variable_duration  true: the speed comes from the function "duration_func" (a name the game knows)
//   link               0..255, status that follows this one; 255 = none; otherwise below the number of entries
//   speed              keyframes per second (a number); speed_q12 = speed * 4096, truncated like Q12()
//   speed_q12          the same speed as the integer the game stores. Give speed, speed_q12 or both (then equal)
//   start_kf, end_kf   -32768..32767; start_kf -1 = from the current keyframe

#include "engine/tables/sh_tables.h"
#include "formats/common/json.h"
#include "formats/common/status.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::tables {

enum class AnimKind : std::uint8_t { BlendLinear = 0, PlaybackOnce = 1, PlaybackLoop = 2, None = 3 };

std::string_view anim_kind_name(AnimKind kind);

struct AnimEntry {
    AnimKind kind = AnimKind::None;
    std::uint8_t status = 0;
    bool variable_duration = false;
    std::uint8_t link = 0;
    std::int32_t speed_q12 = 0;      // unused when variable_duration
    std::string duration_func;       // only when variable_duration
    std::int16_t start_kf = 0;
    std::int16_t end_kf = 0;

    bool operator==(const AnimEntry&) const = default;
};

struct AnimTable {
    std::string symbol;
    std::vector<AnimEntry> entries;  // info.fileCount entries
};

// The table as the disc holds it: the first fileCount 16-byte PSX records at `va` in a map overlay (base
// 0x800C9578). Fails when the overlay is too short or a record is not a table entry (unknown playback function,
// a variable duration no function has).
Result<AnimTable> anim_table_from_overlay(const ShAnimTableInfo& info, std::span<const std::uint8_t> overlay,
                                          std::uint32_t va, std::span<const ShAnimFunc> funcs);

// "MAP5_S02" for the source "VIN/MAP5_S02.BIN".
std::string overlay_of(const ShAnimSource& source);

// The file text for `table` (pretty, one entry per line, ends with a newline). With `overlay` the file is the
// variant of that map overlay and names its copy as the source.
std::string write_anim_table(const AnimTable& table, const ShAnimTableInfo& info, std::string_view overlay = {});

// Reads and checks a file. The error names the entry and the field: entry 3, "speed_q12": ... (line 20, column 9).
Result<AnimTable> read_anim_table(std::string_view text, const ShAnimTableInfo& info, std::span<const ShAnimFunc> funcs);

// The game array: the file entries; the entries past fileCount are zero (no status reaches them).
std::vector<ShAnimEntry> to_game_entries(const AnimTable& table, const ShAnimTableInfo& info,
                                         std::span<const ShAnimFunc> funcs);

// Notes on entries that differ from the reference and break the usual table shape (status of entry i is i,
// a blend entry links to status + 1, a played entry has a playback function). Not errors: the original
// tables have exceptions (docs/FORMATS.md).
std::vector<std::string> lint_anim_table(const AnimTable& table, const AnimTable& reference);

// Indices of the entries that differ.
std::vector<std::size_t> diff_anim_tables(const AnimTable& a, const AnimTable& b);

// Compares the first `count` entries with the 16-byte PSX records at `va` in a map overlay (base 0x800C9578).
// One line per differing entry; an error when the overlay is too short.
Result<std::vector<std::string>> compare_with_overlay(std::span<const ShAnimEntry> entries,
                                                      std::span<const std::uint8_t> overlay, std::uint32_t va);

}  // namespace sh::tables
