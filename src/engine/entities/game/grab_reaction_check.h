// SPDX-License-Identifier: GPL-3.0-or-later
//
// Whether the player's reaction to a grab can be animated on the current map.
//
// A monster's grab takes the player through reaction animations kept in the map's own animation file, found
// through the map's table of player reactions. A map carries only the reactions of the monsters that live on it.
// When a monster is put on another map (the console's spawn, a mod), its grab finds no row: the grab state starts
// nothing and leaves the row cursor where the last lookup put it, and the release state then waits for a keyframe
// of that unrelated row, which never comes - the player stays held for good. Supplying a row without its
// keyframes would not help: the player would run through animations that mean something else.
//
// So a grab is allowed only when every animation of its chain has a row, and is replaced by the ordinary hit
// reaction otherwise - if that one has a row.
#pragma once

namespace sh::entities {

// `attack` is the attack the player received; one that is no grab is always playable. `frontal`: it came from
// the front.
bool grab_reaction_playable(int attack, bool frontal);

// The ordinary torso hit that stands in for a refused grab: it goes through the same table, and a map may lack it.
bool hit_reaction_playable(bool frontal);

}  // namespace sh::entities
