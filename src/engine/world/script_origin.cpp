// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/world/script_origin.h"

#include <map>
#include <utility>

namespace sh::world {

namespace {
std::map<int, ScriptSpawn>& spawns() {
    static std::map<int, ScriptSpawn> s;
    return s;
}
}  // namespace

void note_script_spawn(int slot, ScriptSpawn spawn) { spawns()[slot] = std::move(spawn); }

const ScriptSpawn* script_spawn(int slot) {
    const auto it = spawns().find(slot);
    return it != spawns().end() ? &it->second : nullptr;
}

void clear_script_spawns() { spawns().clear(); }

}  // namespace sh::world
