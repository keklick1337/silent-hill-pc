// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Weapon cycling step (the cycle-weapon binds and the mouse wheel): the next OWNED entry of a
// weapon order, wrapping, skipping what the player does not hold. The order
// itself (which items, in what sequence) stays with the caller, addressed by
// the game's item numbers.

#pragma once

#include <cstddef>
#include <cstdint>

namespace sh::items {

// Index into `order` of the first entry the caller owns, strictly after `start`
// walking `direction` (+1/-1), wrapping, visiting every entry exactly once.
// `start` is an index into order, or -1 when the current weapon is not listed
// (the walk then starts at the front for +1 and just before the front for -1,
// as the weapon wheel does). Returns the index, or -1 when nothing is owned.
// `owned` answers "the inventory holds this item" (a live slot, not a ghost);
// returns 1/0 — plain int so a C ABI region can implement it too.
int weapon_cycle_next(const int* order, std::size_t count, int start, int direction,
                      int (*owned)(void* ctx, int item), void* ctx);

}  // namespace sh::items
