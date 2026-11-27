// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Where a thing a script made at run time comes from: the script file and the line of the call that made it
// (sh.world.pickup_create, sh.world.mark, sh.entity.spawn), noted when it is made so the modding tools can name the
// place to edit. Nothing in the game reads it.

#pragma once

#include <cstdint>
#include <string>

namespace sh::world {

struct ScriptOrigin {
    std::string file;  // the script's host path; empty: not made by a script (or by a chunk without a file)
    int line = 0;      // 1-based line of the call
    // The Lua callers above it, nearest first: "<file name>:<line>" joined by " < " (a table is often placed by a
    // loop in one function that another one calls).
    std::string via;

    bool known() const { return !file.empty() && line > 0; }
};

// A character a script spawned (sh.entity.spawn), by the slot it got. The slot is reused once the character is
// gone: the reader compares `chara` with what stands in the slot now.
struct ScriptSpawn {
    std::string chara;          // the name the script asked for (a mod's own with its prefix)
    std::string owner;          // mod id, "" for the base scripts
    std::int32_t x = 0, z = 0;  // Q12, where it was put
    ScriptOrigin origin;
};
void note_script_spawn(int slot, ScriptSpawn spawn);
// Null: the slot's character was not made by a script on this map.
const ScriptSpawn* script_spawn(int slot);
void clear_script_spawns();

}  // namespace sh::world
