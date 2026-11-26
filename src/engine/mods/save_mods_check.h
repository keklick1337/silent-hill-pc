// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#pragma once

// A save against the mods of this run: which mods it was made with that are missing, switched off or older now
// (a warning: the save list marks it and the load asks first), newer now, or which story mods are active now that
// it was made without (a note only). Pure: tested on plain values.

#include "engine/save/saved_mods.h"

#include <optional>
#include <string>
#include <vector>

namespace sh::mods {

// A mod of this run as the check sees it.
struct InstalledMod {
    std::string id;
    std::string name;
    std::string version;
    bool active = false;
    bool gameplay = false;
};

enum class ModChange {
    Missing,     // the save's mod is not installed (warn)
    NotEnabled,  // installed but switched off or refused (warn)
    OlderNow,    // an older version than the save's is active (warn)
    NewerNow,    // a newer version is active (note)
    AddedNow,    // a GAMEPLAY CHANGE mod is active that the save was made without (note)
};

struct ModDifference {
    ModChange change = ModChange::Missing;
    std::string id;
    std::string name;           // the save's name of it, else this run's, else the id
    std::string saved_version;  // empty for AddedNow
    std::string version_now;    // empty for Missing
};

struct ModCheck {
    std::vector<ModDifference> differences;
    bool warn = false;  // a mod of the save is missing, off or older: the load asks first
    bool note = false;  // only a newer version or a story mod added: a note, the load goes on

    bool empty() const { return differences.empty(); }
};

// `saved`: the save's list (none: a save that does not say, which checks clean). `now`: every installed mod.
ModCheck check_save_mods(const std::optional<std::vector<save::SavedMod>>& saved, const std::vector<InstalledMod>& now);

// One line for the log: "cybil_story 1.2.0: not enabled; hd_harry 1.0.0: not installed". Empty when clean.
std::string describe_check(const ModCheck& check);

// The run's installed mods (the registry's plan: active ones and the others), and the active ones as a save records
// them.
std::vector<InstalledMod> installed_mods();
std::vector<save::SavedMod> active_mods_for_save();

}  // namespace sh::mods
