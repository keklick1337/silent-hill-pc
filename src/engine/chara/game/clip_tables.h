// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The characters' clip tables of the running game (docs/FORMATS.md): one table per registry entry
// (engine/tables/anim_table_registry.h), filled from gamedata/tables/anim/ at boot and again with every map load (on
// the PS1 each map overlay carries its own copy of the tables it uses, so a change the game made to an entry at run
// time is gone after the next load). The entries name their playback by kind; the table holds the engine's playback
// of that kind.

#pragma once

#include <span>
#include <string_view>

#include "engine/chara/game/clip_info.h"
#include "engine/tables/sh_tables.h"

namespace sh::chara::game {

// The engine's playback of each kind of ShAnimEntry::kind (0 blend into, 1 once, 2 loop); nullptr for "none".
struct ClipPlaybacks {
    ClipPlayback blend = nullptr;
    ClipPlayback once = nullptr;
    ClipPlayback loop = nullptr;
};

// Writes the table entries into `clips` (zero beyond them is left as it is): the kind picks the playback, the other
// fields are copied, a variable duration keeps the value the file gave (the game resolves it).
inline void fill_clip_entries(std::span<ClipInfo> clips, std::span<const ShAnimEntry> entries,
                              const ClipPlaybacks& playbacks) {
    for (std::size_t i = 0; i < entries.size() && i < clips.size(); ++i) {
        const ShAnimEntry& e = entries[i];
        ClipInfo& c = clips[i];
        c.playback = e.kind == 0 ? playbacks.blend : e.kind == 1 ? playbacks.once : e.kind == 2 ? playbacks.loop : nullptr;
        c.status = e.status;
        c.variable_speed = e.hasVariableDuration;
        c.link = e.linkStatus;
        c.speed = e.durationQ12;
        c.start_keyframe = e.startKeyframeIdx;
        c.end_keyframe = e.endKeyframeIdx;
    }
}

// The engine's playbacks (engine/entities/game/model_runtime.cpp).
ClipPlaybacks engine_clip_playbacks();

// Fills every table once (boot, before any map loads; Harry's base clips are read earlier by
// sh::inventory::game::load_harry_clip_table).
void clip_tables_init();
// A map was loaded (`map` is its registry name, "map5_s02"): every table but the Puppet Nurse's two is filled again
// with that map's copy. Runs before any code of the new map.
void clip_tables_map_loaded(const char* map);
// The table of registry name `name` ("monster_cybil"); empty when there is none.
ClipTableRef clip_table(std::string_view name);

}  // namespace sh::chara::game
