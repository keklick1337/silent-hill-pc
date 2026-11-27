// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Shared between the two files of the placement editor: what can be selected on the loaded map (edit_things.cpp:
// the list, the live change of one thing, where it is written and its corrected line) and the editor itself
// (placement_editor.cpp: the panel, the handles in the world, the free cursor and the path).

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/devtools/placement_edit.h"
#include "features/tools/tools.h"

namespace sh::tools::edit {

namespace dt = sh::devtools;

enum class Kind : std::uint8_t { Object, Pickup, Character, Zone, Trigger, Point, Spawn, Count };
const char* kind_name(Kind k);   // one word, the console's and the list's
const char* kind_title(Kind k);  // the filter's label

enum Caps : unsigned {
    kMove = 1,     // x and z
    kHeight = 2,   // y
    kYaw = 4,      // the angle about the vertical
    kTilt = 8,     // the other two angles
    kRadius = 16,  // a circle's radius, a segment's half width
    kSize = 32,    // a box's two half extents
};

struct Thing {
    Kind kind = Kind::Object;
    std::string key;    // unique on the map: "object:CybilBike", "zone:cybil_story:spot9", "chara:3"
    std::string label;  // its name
    std::string does;   // what it is for, one line
    std::string id;     // the engine's name of it (object name, pickup id, mark id)
    std::string owner;  // the mod that made it, when the engine knows
    int index = -1;     // slot, row or index of it
    std::array<std::int32_t, 3> position{};  // Q12
    std::array<std::int16_t, 3> rotation{};  // Q12 turns
    std::int32_t radius = 0;                 // Q12
    std::array<double, 2> size{};            // metres: a box's half extents along x and z
    double size_step = 0.0;                  // the grid the size and the radius are stored on (0: Q12)
    unsigned caps = 0;
    bool shown = true;       // drawn or in force now (its flags, its cells)
    bool on_ground = false;  // drawn at the ground's height: it has none of its own
};

// Everything of the loaded map; `ground_y` (metres) is where the things without a height are drawn.
std::vector<Thing> collect(double ground_y);
// The thing's numbers in the form its file writes them: the shortest metres and degrees that read back.
dt::EditValue value_of(const Thing& thing);
// Where a thing is drawn and grabbed (metres).
Vec3 place_of(const Thing& thing);
// Puts the value on the thing in the running game. Only from a deferred update.
void apply(const Thing& thing, const dt::EditValue& value);

// One place a thing is written at, with the text as it should stand after the edit.
struct Correction {
    std::string where;   // "mod cybil_story: scripts/main.lua:655"
    std::string before;  // the line or member as the file has it; empty: nothing there yet
    std::string after;   // as it should stand; empty: nothing to change there
    std::string record;  // JSON: the whole record as it should stand
    std::string note;
};

// Where the thing comes from. Built in a deferred update (it reads files), then only read.
struct Origin {
    std::string key;
    std::vector<std::string> rows;  // what the panel shows: who made it, which file and line hold it
    // JSON data: the files that have a record of the thing, lowest layer first.
    struct Layer {
        std::string file, where, text;
        dt::TextSpan record;
        int line = 0;
        bool position = false, rotation = false, pose = false;
    };
    std::vector<Layer> layers;
    int target = -1;  // the layer whose record holds the place
    // A script: the file of the call that made the thing, and the lines that hold its numbers.
    std::string lua_file, lua_where, lua_text, lua_mod;
    std::vector<dt::SourceLine> place_lines, size_lines;
    int place_pick = 0, size_pick = 0;
    bool spawned = false;  // `spawn` is where the script put the character (it has walked since)
    std::array<std::int32_t, 2> spawn{};
    std::string plain_where, plain_before;  // map data written as one entry (points.json, spawns.json)
};
// `file` is what the thing had before any edit: the numbers to look for in its script.
Origin load_origin(const Thing& thing, const dt::EditValue& file);
// The lines to change for `now`, given what the thing had in its file (`original`). Empty: nothing changed.
std::vector<Correction> corrections(const Thing& thing, const Origin& origin, const dt::EditValue& original,
                                    const dt::EditValue& now);

}  // namespace sh::tools::edit
