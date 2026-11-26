// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#pragma once

// One mod a save was made with (the META chunk's "mods", save_store.h): what the save list and the load compare with
// the mods of the run (engine/mods/save_mods_check.h).

#include <string>

namespace sh::save {

struct SavedMod {
    std::string id;
    std::string name;     // shown to the player (the id when the mod has none)
    std::string version;  // as its mod.json writes it
    bool gameplay = false;  // a GAMEPLAY CHANGE mod (rules, balance or story)

    friend bool operator==(const SavedMod&, const SavedMod&) = default;
};

}  // namespace sh::save
