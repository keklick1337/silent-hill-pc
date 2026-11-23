// SPDX-License-Identifier: GPL-3.0-or-later
//
// Text of the item screens: item names and descriptions as game text
// (the disc's English of the import, gamedata/text/<lang>/items.json, a disc or pack translation first), the
// inventory's map notices and the layout of gamedata/menu/item_screens.json, and the placement rules that keep translated labels from running
// into their neighbours. Drawn with the game's text drawer (sh::text::game_text()).

#pragma once

#include <cstdint>

#include "engine/screens/item_screen_style.h"

namespace sh::inventory {

// gamedata/menu/item_screens.json (a mod's copy wins), read on first use; the built-in layout when it cannot be.
const screens::ItemScreenStyle& item_screen_style();

// The name and description of an item by its number ("" when there is none).
const char* item_name_text(int item);
const char* item_description_text(int item);

// How much wider (pixels) the translation of the English menu label `english` is; 0 when it is not wider.
std::int32_t label_growth(const char* english);

// x of the item name after the "Name:" label.
std::int32_t item_name_x();

// Places the drawer for the tag `tag` after the item's name (x of the tag in English); returns the y.
std::int32_t place_name_row_tag(std::int32_t x, const char* tag, int item);

// The inventory's MAP notice: "too dark to look at the map" or (no_map) "no map for this place".
void draw_map_notice(bool no_map);

}  // namespace sh::inventory
