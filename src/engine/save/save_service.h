// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#pragma once

// The store the game opened (ShSave_Open, save_c.h), for C++ code: the save list of the save screen.

#include "engine/save/save_store.h"

namespace sh::save {

SaveStore& active_store();

// The mods the next save writes into its META (GameSaves sets them from its host before each write); nothing: the
// save does not say.
void set_outgoing_mods(std::optional<std::vector<SavedMod>> mods);

}  // namespace sh::save
