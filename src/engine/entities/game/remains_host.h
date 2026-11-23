// SPDX-License-Identifier: GPL-3.0-or-later
// Persistent remains over the running game (engine/entities/remains.h, config `persistent_remains`): what the NPC
// main loop and the room change tell it, and what it does to the session's character records and the map's effect
// slots. With the option off every entry point returns at once and the game is the original's.

#pragma once

#include <string>
#include <string_view>

#include "engine/entities/remains.h"

namespace sh::entities::game::remains {

RemainsStore& store();
bool enabled();

// NPC slot `slot` holds a stored body standing in the world: a statue no rule of the game moves, frees or hears.
bool is_statue(int slot);

// Once per NPC tick, after the slots ran: notes the bodies lying in the world (and the pools under them) and brings
// stored bodies near the player back. `is_monster(chara)`: the character is an enemy.
void tick(bool (*is_monster)(int chara));
// The game lets the body in `slot` go (too far away): it is kept, and its pools stay on the floor.
void slot_freed(int slot);
// The NPC slots are about to be wiped (a room or map change): every body lying there is kept.
void room_left();
// The room's first spawn pass ran: its stored pools go back on the floor.
void room_entered();
// A new game: nothing lies anywhere.
void session_reset();

// The save's RMNS chunk: empty when nothing is stored.
std::string save_text();
// A loaded save's chunk (empty: the save has none, nothing lies anywhere).
void load_text(std::string_view text);

}  // namespace sh::entities::game::remains
