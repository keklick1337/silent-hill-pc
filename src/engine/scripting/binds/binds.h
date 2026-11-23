// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The named script binds of the map scripts, by family. install_named_binds registers every
// family with the engine registry (engine/script/named_binds.h) and chains it into the game API.

#pragma once

#include <span>

#include "engine/script/named_binds.h"

namespace sh::script::binds {

std::span<const named::Bind> game_binds();     // session, game state, camera, Harry
std::span<const named::Bind> music_binds();    // music mix, sound driver, positional music
std::span<const named::Bind> world_binds();    // map effects, effect slots, world objects, glow fields
std::span<const named::Bind> chara_binds();    // characters, character types, loads, map spawn data
std::span<const named::Bind> math_binds();     // the game's integer math
std::span<const named::Bind> ending_binds();   // the final map's effects and cues
std::span<const named::Bind> panel_binds();    // textured panels drawn in the world

}  // namespace sh::script::binds

// Registers the families and puts them in front of the api's named functions (once, at script API setup).
extern "C" void ShScriptBinds_Install(ShScriptGameApi* api);
