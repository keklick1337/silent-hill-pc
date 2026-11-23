// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The state dump of the debug console (`dump`): a folder <exe dir>/dumps/<id>/ with what the
// game looked like when a bug was seen (dump.json, shot.png, state.sav), and the way back into it (`dump load`).
// The document and its id are sh::devtools (engine/devtools/state_dump.h); this fills it from the live game.

#pragma once

#include "formats/common/status.h"

#include <string>
#include <string_view>

namespace sh::dump {

struct Written {
    std::string id;
    std::string folder;
};

// Takes the dump now. `note` is the reporter's text, kept in dump.json.
Result<Written> write(std::string_view note);

// Queues the restore of dump `id`: its state.sav is loaded as soon as the game can load a save (at once in
// gameplay; a line of -consolecmd at start waits for the map), then the player is put where dump.json says.
// Returns the dump's folder.
Result<std::string> load(std::string_view id);

// Once per frame: carries a queued load through.
void tick();

}  // namespace sh::dump
