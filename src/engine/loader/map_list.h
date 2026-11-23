// SPDX-License-Identifier: GPL-3.0-or-later
//
// The maps of the game, in the order of gamedata/game.json ("maps": name and description of each; mods patch the
// list). A map's index in that list is the number the game's own data uses for it (saves hold the name).
#pragma once

namespace sh::loader {

int map_count();

// "map0_s00"; "unknown" for an index outside the list.
const char* map_name(int index);

// What the map is, for people ("Old Silent Hill - cafe"); "" outside the list.
const char* map_description(int index);

// The index of the map with this name, or -1.
int find_map(const char* name);

// Makes the map the current one and loads it. A map that cannot be loaded ends the game with a message: nothing
// can run without its data.
void load_map(int index);

// At start: reads the list, then loads the map the config names (`map`), or the first one when that names none.
void load_start_map();

}  // namespace sh::loader
