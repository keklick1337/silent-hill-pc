// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// sh.engine: what a script may ask about the engine it runs on (engine/version/engine_version.h). Plain values,
// the same in the game scripts and in the menu scripts.
//
//   sh.engine.version   "1.0.0"                  the version mod.json requires with "min_engine_version"
//   sh.engine.major     1
//   sh.engine.minor     0
//   sh.engine.patch     0
//   sh.engine.build     "1.0.0+2890.1a2b3c4d5"   with the build number and commit, for logs
//   sh.engine.at_least(major [, minor [, patch]]) -> true when the engine is that version or newer
#pragma once

struct lua_State;

namespace sh::script {

// Sets field "engine" of the table on top of the stack (the `sh` table).
void open_engine_api(lua_State* L);

}  // namespace sh::script
