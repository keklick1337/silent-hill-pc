// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The clip tables of the character types: character.json "clip_tables" names them, the
// first is the type's default. A name is a table of gamedata/tables/anim ("cybil", the game's copy the map loads
// refill) or a clip file of gamedata/chara ("AirScreamer/clips.json", the same format, read once; types naming one
// file share its table). A clip file's variable speeds name functions the behaviour hosts register.

#pragma once

#include <string_view>

#include "engine/chara/behavior_host.h"
#include "engine/chara/game/clip_info.h"

namespace sh::chara::game {

// The behaviour host's port: table `index` of the type's character data.
bool data_clip_table(int chara_id, int index, sh::chara::behavior::ClipTable* out);

// Table `index` of the type (the character data's, then the tables extensions register); false past the last.
bool type_clip_table(int chara_id, int index, ClipTableRef* out);
// The table a record of the type plays from: the one `active` points at when it is one of the type's, else the
// type's first; empty when the type has none.
ClipTableRef active_clip_table(int chara_id, const void* active);

// A variable speed a clip file may name in "duration_func"; register before the first table is read.
void register_clip_speed(std::string_view name, ClipSpeedFn fn);

}  // namespace sh::chara::game
