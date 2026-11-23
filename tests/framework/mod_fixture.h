// New mods are switched off until the player switches them on (load_order.h). Tests of what an active mod does
// switch every mod of their scratch folder on, the way the Mods screen would.
#pragma once

#include "engine/mods/registry.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace sh::test {

// Writes <mods_dir>/load_order.json with every mod in it switched on, ordered as the engine orders mods nobody
// moved (priority, then id), so the tests keep the order their manifests ask for. Call again after adding a mod.
inline void enable_mods(const std::filesystem::path& mods_dir) {
    std::vector<std::string> warnings;
    std::vector<sh::mods::Candidate> found = sh::mods::scan_mod_dirs({mods_dir}, warnings);
    std::stable_sort(found.begin(), found.end(), [](const sh::mods::Candidate& a, const sh::mods::Candidate& b) {
        if (a.manifest.priority != b.manifest.priority) {
            return a.manifest.priority < b.manifest.priority;
        }
        return a.manifest.id < b.manifest.id;
    });
    sh::mods::UserState state;
    for (const sh::mods::Candidate& c : found) {
        if (state.find(c.manifest.id) == nullptr) {
            state.mods.push_back({c.manifest.id, true});
        }
    }
    std::ofstream(mods_dir / std::string(sh::mods::kUserStateFile), std::ios::binary | std::ios::trunc)
        << sh::mods::write_user_state(state);
}

// The scratch-file writers of the tests call this after each file: a manifest written switches the mods of its
// mods folder on.
inline void mod_file_written(const std::filesystem::path& file) {
    const std::filesystem::path name = file.filename();
    if (name == "mod.json" || name == "mod.cfg") {
        enable_mods(file.parent_path().parent_path());
    }
}

}  // namespace sh::test
