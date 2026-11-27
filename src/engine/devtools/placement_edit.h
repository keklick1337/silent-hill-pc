// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The text and number work of the modding tools' placement editor (src/features/tools/placement_editor.cpp), kept
// apart from its UI: where a thing is written (the base data or a mod, the record of a JSON file, the line of a Lua
// script that holds its numbers), the same record or line with the moved thing's numbers in it, a world point in
// metres and in the game's Q12 integers, the lengths and times of a path, and the step-back list of the edits.
//
// A file is never parsed into values and written again: the numbers of the record are replaced where they stand, so
// the layout, the other keys and the comments of the author's file stay as they are.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sh::devtools {

// --- numbers ---

// A decimal with at most `decimals` places, trailing zeros dropped; `keep_point` leaves one ("3.0").
std::string tidy_number(double value, int decimals = 4, bool keep_point = false);

// A Lua table of a world point (metres) in the game's integers, as Q12() reads the metres, with the metres as the
// comment: "{ x = -40, y = -2, z = -74729 } -- -0.01, -0.0005, -18.2444 m".
std::string raw_point_row(double x, double y, double z);

// --- which file ---

// A host file as the data names it: the base game's or a mod's, and the path below that root.
struct FileOwner {
    std::string mod;       // "" for the base data
    std::string relative;  // "maps/map2_s00/world_objects.json", "scripts/main.lua"
    bool known = false;    // the file lies under the base data or under one of the mods
};
// `mods` are (id, root folder) pairs; the deepest root that holds the file wins.
FileOwner file_owner(std::string_view file, std::string_view gamedata_dir,
                     const std::vector<std::pair<std::string, std::string>>& mods);
// "base gamedata/maps/..." or "mod cybil_story: scripts/main.lua".
std::string file_owner_text(const FileOwner& owner);

// --- number literals of a line ---

struct NumberToken {
    std::size_t begin = 0, end = 0;  // [begin, end) in the text, the sign included
    double value = 0.0;
    bool integer = false;  // written without a point or an exponent
};
// The number literals of Lua or JSON text outside its strings and (Lua) comments. A '-' belongs to the literal when
// nothing before it could be its left operand.
std::vector<NumberToken> number_tokens(std::string_view text, bool lua = true);

struct SourceLine {
    int line = 0;  // 1-based
    std::string text;
};
// 1-based line of an offset.
int line_of(std::string_view text, std::size_t offset);

// A value a line should hold, in the unit the file writes (metres, degrees) with how far a literal may be from it.
// A script may also write a position as a Q12 integer: with `q12` an integer literal near value * 4096 counts.
struct Wanted {
    double value = 0.0;
    double tolerance = 0.0;
    bool q12 = false;
};
// The lines of `source` that hold a literal of their own for each wanted value.
std::vector<SourceLine> lines_with_values(std::string_view source, const std::vector<Wanted>& wanted);
// The lines that hold `text` as a quoted string.
std::vector<SourceLine> lines_with_string(std::string_view source, std::string_view text);

// One number of a line to rewrite. `key` prefers the literal written as `key = <n>` or `"key": <n>` when several
// literals match (the radius of a zone among its coordinates).
struct Change {
    Wanted from;
    double to = 0.0;
    std::string key;
};
// The line with each changed value's literal rewritten in the unit it was written in (a Q12 integer stays one), at
// most 4 decimals. `replaced` counts the literals found; a change without a literal leaves the line as it was.
std::string patch_line(std::string_view line, const std::vector<Change>& changes, int* replaced = nullptr);

// --- JSON records ---

struct TextSpan {
    std::size_t begin = 0, end = 0;
    bool empty() const { return end <= begin; }
};
// The object whose own member `id_key` is the string `id` ("name": "CybilBike") or the bare literal `id`
// ("index": 5), braces included.
std::optional<TextSpan> json_record(std::string_view text, std::string_view id_key, std::string_view id);
// The value of the record's own member `key` (not of an object inside it).
std::optional<TextSpan> json_member(std::string_view text, TextSpan record, std::string_view key);

// New numbers for one member of a record ("position": [x, y, z]).
struct MemberEdit {
    std::string key;
    std::vector<double> values;
    int decimals = 4;
    // keep[i]: the i-th number did not change, its literal stays as the file writes it (empty: all are rewritten).
    std::vector<bool> keep = {};
};
struct PatchedRecord {
    int line = 0;                    // of the record's opening brace
    std::string record;              // the whole record as it should stand
    std::vector<SourceLine> before;  // each edited member as the file has it: `"position": [1, 2, 3]`, its line
    std::vector<std::string> after;  // the same members with the new numbers (one line each)
    std::vector<std::string> added;  // members the record does not have yet, to add to it
};
PatchedRecord patch_json_record(std::string_view text, TextSpan record, const std::vector<MemberEdit>& edits);

// --- paths ---

struct PathPoint {
    double x = 0, y = 0, z = 0;  // metres, y down
};
std::vector<double> path_segments(const std::vector<PathPoint>& points);
double path_length(const std::vector<PathPoint>& points);
// Seconds to travel `length` at `speed` (m/s), and the speed that takes `seconds`; 0 when the divisor is not positive.
double path_time(double length, double speed);
double path_speed(double length, double seconds);
// The points as a Lua table of Q12 integers (one raw_point_row a line), of metres, and as a JSON array of metres.
std::string path_lua_raw(const std::vector<PathPoint>& points);
std::string path_lua_metres(const std::vector<PathPoint>& points);
std::string path_json(const std::vector<PathPoint>& points);

// --- step back ---

struct EditValue {
    std::array<double, 3> position{};  // metres
    std::array<double, 3> rotation{};  // degrees
    double radius = 0.0;               // metres
    std::array<double, 2> size{};      // metres, a box zone's half extents
    bool operator==(const EditValue&) const = default;
};
struct EditStep {
    std::string key;  // which thing
    EditValue before, after;
};
class EditHistory {
public:
    // Notes a change of `key`. While one drag lasts (`gesture` the same, not 0) its steps fold into one.
    void record(const std::string& key, const EditValue& before, const EditValue& after, int gesture = 0);
    // The step to take back (apply its `before`), or nothing.
    std::optional<EditStep> undo();
    std::optional<EditStep> redo();
    bool can_undo() const { return at_ > 0; }
    bool can_redo() const { return at_ < steps_.size(); }
    std::size_t size() const { return at_; }
    // What `key` was before its first edit (the file's values); nothing when it was never edited.
    std::optional<EditValue> original(const std::string& key) const;
    void clear();

private:
    std::vector<EditStep> steps_;
    std::size_t at_ = 0;  // steps_[0, at_) are done
    int gesture_ = 0;
    std::map<std::string, EditValue> originals_;
};

}  // namespace sh::devtools
