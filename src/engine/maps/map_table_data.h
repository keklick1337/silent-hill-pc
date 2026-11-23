#pragma once

// Readable data of tables.json arrays: an array entry may carry its contents as named records
// instead of leaving them to the disc overlay. The data is written over a copy of the overlay's bytes at the
// array's place (va - base), in the disc layout, so everything that reads the overlay (MapTables::fill, the
// reaction import of other maps) sees the data: a mod that patches the records changes the game, and an entry
// without data keeps the disc's bytes.
//
// The data keys of a symbol entry (one per entry; bytes the data does not cover are zero):
//   "items":            [item name, ...]                      a u8 list of item numbers (LOADABLE_INVENTORY_ITEMS)
//   "clips":            [{"playback": "blend"|"once"|"loop", "status", "link", "speed" | "speed_q12" |
//                         "speed_function": "variable_duration", "start_keyframe", "end_keyframe"}]  s_AnimInfo
//   "reaction_rows":    [{"reaction": name or id, "status", "next_status", "start_keyframe", "last_keyframe"}]
//   "particle_emitter", "smoke_column", "water_stream": {fields}   one effect parameter block
//   "glass_panes", "drips":                  [{fields}]            effect record lists
// Every record key is checked: an unknown or missing key is an error, and so is a value out of its field's range.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

namespace sh::maps {

// The data keys a symbol entry may have (load_registry accepts them).
inline constexpr std::string_view kTableDataKeys[] = {"items",        "clips",        "reaction_rows", "particle_emitter",
                                                      "smoke_column", "water_stream", "glass_panes",   "drips"};

// Names in the data: the number of an item and the id of one of Harry's reactions (chara/Harry/reactions.json);
// -1 when unknown.
class TableNames {
public:
    virtual ~TableNames() = default;
    virtual int item(std::string_view name) const = 0;
    virtual int reaction(std::string_view name) const = 0;
};

struct TableDataResult {
    std::vector<std::uint8_t> overlay;  // the overlay with every data array written in
    int arrays = 0;                     // how many arrays carried data
};

// `overlay`: the disc overlay's bytes (may be empty: the result then holds only the data, zero elsewhere).
Result<TableDataResult> apply_table_data(std::string_view tables_json, std::span<const std::uint8_t> overlay,
                                         const TableNames& names);

}  // namespace sh::maps
