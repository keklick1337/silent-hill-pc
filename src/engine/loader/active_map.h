// SPDX-License-Identifier: GPL-3.0-or-later
//
// The loaded map. Every map is data (gamedata/maps/<map>/): a load reads
// it into the running map (engine/maps/map_runtime.h); this module knows which map that is and finds its named
// data.
#pragma once

namespace sh::loader {

// Loads the map with this index of the map list (map_list.h) into the running map, replacing the loaded one.
// False on failure; active_map_error() then says why and no map counts as loaded.
bool load_active_map(int map_index);

// The loaded map's name, or nullptr when none is.
const char* active_map_name();

// Why the last load_active_map failed; "" when none has.
const char* active_map_error();

// A named piece of data of the loaded map, each map having its own of the same name: the running map's points and
// effect slots, an array of the map's disc tables (tables.json), a run-long word of the maps
// (config/map_globals.json, with the records the engine binds there). nullptr when no map is loaded or nothing
// has the name.
void* active_map_symbol(const char* name);

}  // namespace sh::loader
