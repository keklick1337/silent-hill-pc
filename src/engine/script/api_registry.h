// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The engine-owned naming of the script API: every flat sh.<legacy> function that the host
// binds has one canonical "<group>.<name>" (sh.flag.get, sh.player.pos ...). The legacy flat
// name keeps working as an alias; the game layer only supplies the host implementation behind
// the function pointers in GameApi. Game-agnostic: this table names capabilities, not structs.

#pragma once

#include <cstddef>
#include <span>

namespace sh::script {

struct ApiAlias {
    const char* group;   // canonical table: sh.<group>
    const char* name;    // canonical function: sh.<group>.<name>
    const char* legacy;  // the flat function it names: sh.<legacy>
};

// Sorted by group; one entry per binding. Families: flag, item, player, map, audio, ui, input.
std::span<const ApiAlias> api_aliases();

}  // namespace sh::script
