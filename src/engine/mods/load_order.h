// Which mods load and in what order. Pure: no files, so every rule is
// tested on plain values.
//
// Rules, in this order:
//   1. Two folders with one id: the first folder by name keeps it, the other is an error.
//   2. A mod made for another engine version (min_engine_version, max_engine_version, the engine range) is an
//      error whatever its switch says: the Mods screen shows why before the player switches it on.
//   3. A mod the player switched off (UserState), or never switched on (a newly installed mod), is Disabled;
//      a switched-on mod whose api / game requirement does not hold is an error.
//   4. Repeated until nothing changes: a required dependency that is missing, not active or out of range, an
//      enabled mod it conflicts with (the mod that names the conflict is the one switched off), or a
//      cycle of required dependencies makes the mod an error. Mods depending on it follow.
//   5. The rest is sorted: required and optional dependencies and load_after / load_before first, then the
//      player's order (UserState), then priority (low first), then id. A load_after / load_before hint that would
//      close a cycle is dropped with a warning.
// Later in the order = higher precedence: the last mod that has a file wins.
#pragma once

#include "engine/mods/manifest.h"
#include "engine/version/engine_version.h"

#include <filesystem>
#include <string>
#include <vector>

namespace sh::mods {

struct Candidate {
    Manifest manifest;
    std::filesystem::path root;
    std::string folder;                 // folder name in its mods directory
    std::vector<std::string> warnings;  // from reading the manifest
};

// What the player chose (mods/load_order.json): the order of the mods they ordered and which are off.
struct UserEntry {
    std::string id;
    bool enabled = true;
};
struct UserState {
    std::vector<UserEntry> mods;      // in the player's order; mods not listed are new: disabled until switched on
    std::vector<std::string> notices;  // one-time messages already shown (migration)

    const UserEntry* find(std::string_view id) const;
};

// The version of this engine (engine/version/engine_version.h): what "min_engine_version", "max_engine_version"
// and the "engine" range of a manifest are checked against.
inline const Version kEngineVersion{version::kMajor, version::kMinor, version::kPatch, {}};

struct Environment {
    Version engine = kEngineVersion;
    Version api{1, 0, 0, {}};
    std::string game;  // disc serial, "" = unknown (requirement not checked)
};

// Why `manifest` cannot run on `engine` ("needs engine 1.2.0 or newer, this is 1.0.0"), one line per requirement
// that does not hold: min_engine_version, max_engine_version, the "engine" range. Empty: it can.
std::vector<std::string> engine_problems(const Manifest& manifest, const Version& engine);

enum class ModState {
    Active,
    Disabled,  // switched off by the player
    Error,     // switched off by the engine; `problems` says why
};

const char* state_name(ModState state);

struct ResolvedMod {
    Candidate candidate;
    ModState state = ModState::Active;
    int load_index = -1;                // place among the active mods, 0 = loads first
    std::vector<std::string> problems;  // why it is not active
    std::vector<std::string> warnings;  // manifest warnings and dropped hints
};

struct Plan {
    std::vector<ResolvedMod> mods;  // active mods in load order, then the others (player order, priority, id)
    std::vector<std::string> warnings;

    std::vector<const ResolvedMod*> active() const;
    const ResolvedMod* find(std::string_view id) const;
};

Plan resolve_load_order(std::vector<Candidate> candidates, const UserState& user, const Environment& env);

}  // namespace sh::mods
