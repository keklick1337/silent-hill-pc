// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Small pure decisions of the map effects: which blood colour slot a hit leaves and which ground types count as
// "special" (hard, splashing surfaces). The game reads the config and the surface and passes plain numbers.

#include <cstdint>

namespace sh::world {

// Colour slot (0..9) of the blood for a hit kind. `extra_color` is the player's extra-blood-colour setting (0..13).
// Kind 15 is always the default colour; some settings override every other kind.
std::int32_t blood_color_slot(std::int32_t kind, std::int32_t extra_color);

// True for the ground types 0, 7 and 12.
bool is_special_ground(std::int32_t ground_type);

}  // namespace sh::world
