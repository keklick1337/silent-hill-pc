// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The game's console commands: registers every command of the debug console (map, give, spawn, flags, camera,
// graphics tuning, ...) into the engine registry (src/engine/console/registry.h) with typed arguments, help and
// live completion providers (maps, characters, items, flags, files). The commands are grouped by subject in
// src/engine/debug/commands. Called once by the console host (console_host.cpp) after the registry exists.

#pragma once

#include "engine/console/registry.h"

namespace sh::debug {

void register_game_commands(sh::console::Registry& registry);

}  // namespace sh::debug
