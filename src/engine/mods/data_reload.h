// SPDX-License-Identifier: GPL-3.0-or-later
// The data the game parsed from gamedata/ and the mods, read again by `mods reload`: one epoch every cache of parsed data compares with, and what changed in the active mods' files.
#pragma once

#include "engine/mods/registry.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sh::mods {

// The generation of the parsed data: 0 at start, one more after each `mods reload` that read the data again.
std::uint32_t data_epoch();
void advance_data_epoch();

// The stamp of a cache never filled yet: data_stale() answers true for it in every epoch.
inline constexpr std::uint32_t kNeverRead = 0xFFFFFFFFu;

// True when the cache stamped `seen` is from an older epoch (or was never filled); stamps it with the current one.
// The caller then drops what it holds and reads the data again.
bool data_stale(std::uint32_t& seen);

// The files of the active mods `mods reload` found added, removed or changed since the previous index, by "<mod
// id>/<path>", sorted by how they apply.
struct ReloadChanges {
    // Texts, menus and the mods' game script: read again in place, play goes on.
    std::vector<std::string> live;
    // Everything else the game parsed (items, weapons, flags, game.json, config/, characters, maps and their
    // scripts, models, textures, sounds): read again between frames; in play the current map loads again in place.
    std::vector<std::string> data;
    // Read only when the game starts (shaders, the boot's own config): after a restart.
    std::vector<std::string> restart;

    bool empty() const { return live.empty() && data.empty() && restart.empty(); }
};

// The change between two indexes of the registry (Registry::file_stamps).
ReloadChanges classify_changes(const Registry::FileStamps& before, const Registry::FileStamps& after);
// How one changed file applies: 0 live, 1 data, 2 restart. `path` is relative to its mod folder.
int change_kind(std::string_view path);

// The changes of the last `mods reload` (hot_reload sets them before the reloaders run).
const ReloadChanges& last_reload_changes();
void set_last_reload_changes(ReloadChanges changes);

}  // namespace sh::mods
