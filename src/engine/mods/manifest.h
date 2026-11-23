// The manifest of a mod: mods/<folder>/mod.json. The old key = value
// mods/<folder>/mod.cfg is still read, with a warning, when there is no mod.json.
#pragma once

#include "engine/mods/semver.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::mods {

inline constexpr std::string_view kManifestFile = "mod.json";
inline constexpr std::string_view kLegacyManifestFile = "mod.cfg";
inline constexpr int kManifestFormat = 1;

// Another mod named by id with the versions that are meant.
struct ModRef {
    std::string id;
    VersionRange versions;
};

struct Manifest {
    std::string id;           // the mod's namespace: "hd_harry" -> ids "hd_harry:Name"
    std::string name;         // shown to the player; defaults to the id
    Version version;
    std::string version_text;  // as written
    std::string description;
    std::vector<std::string> authors;
    std::string homepage;

    // The oldest and the newest engine the mod runs on, MAJOR.MINOR.PATCH (none: no limit).
    std::optional<Version> min_engine_version;
    std::optional<Version> max_engine_version;
    std::optional<VersionRange> engine;  // engine versions as a range, for what the two above cannot say
    std::optional<VersionRange> api;     // mod API versions (none: any)
    std::vector<std::string> games;      // disc serials ("SLUS-00707"); empty: any

    std::vector<ModRef> dependencies;           // must be installed, enabled and in range; load first
    std::vector<ModRef> optional_dependencies;  // used when present and in range; load first
    std::vector<ModRef> conflicts;              // in range and enabled: this mod is disabled
    std::vector<std::string> load_after;        // order hints only
    std::vector<std::string> load_before;
    int priority = 0;  // tie-break of the load order: higher loads later and wins

    bool gameplay = false;  // GAMEPLAY CHANGE: the mod changes rules, balance or story, not only looks
    bool scripts = false;   // the mod runs Lua (scripts/*.lua)
    // False: switching the mod or moving it applies at once (its files are read again on demand: textures, text).
    // Mods with scripts or gameplay rules always need a restart, whatever they say.
    bool requires_restart = true;
    // Layered Lua files (maps/<map>/events.lua, chara/<Name>/behavior.lua...) the mod's copy replaces whole
    // instead of extending; gamedata paths with '/'. Two active mods replacing one path: the later is an error.
    std::vector<std::string> replace;
    std::string icon;       // image paths inside the mod folder
    std::string preview;

    bool legacy = false;  // read from mod.cfg
};

struct ManifestResult {
    std::optional<Manifest> manifest;  // empty: unusable, see errors
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

// Letters, digits, '_', '-', '.', at most 64 bytes, not starting with '.' or '-'. "silenthill" is reserved.
bool valid_mod_id(std::string_view id);

// `folder` is the mod folder's name: it names messages and is the id when the manifest has none (mod.cfg only).
ManifestResult parse_manifest_json(std::string_view text, std::string_view folder);
ManifestResult parse_legacy_cfg(std::string_view text, std::string_view folder);

// The manifest text for `m` (pretty JSON, keys in the documented order).
std::string write_manifest_json(const Manifest& m);

}  // namespace sh::mods
