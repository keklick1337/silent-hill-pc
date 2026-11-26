// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#pragma once

// The version of the game a save was written by (META "game_version", the VERSION file's "1.3.0") against the
// version of the running game. A save of a newer game is not loaded: what a later version adds to a save this one
// would drop or misread. Pure: tested on plain strings.

#include <optional>
#include <string_view>
#include <vector>

namespace sh::save {

// The leading numbers of "1.10.0-rc1+77" (1, 10, 0): one or more runs of digits joined by dots; whatever follows
// them is not part of the comparison. Nothing when the text does not start with a number.
std::optional<std::vector<int>> parse_game_version(std::string_view text);

// -1, 0, 1 as a < b by the numbers, a missing one counting as 0 ("1.3" is "1.3.0"); 1.10.0 is after 1.9.0.
int compare_game_versions(const std::vector<int>& a, const std::vector<int>& b);

// The save says it was made by a later game than `current`. An empty `saved` is a save that does not say (every
// save made before the key existed): older than everything. A `saved` that does not parse is taken for a later
// game's, since no game up to this one wrote such a version.
bool from_newer_game(std::string_view saved, std::string_view current);

}  // namespace sh::save
