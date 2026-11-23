// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Named binds of the characters, the character types of the loaded map, their loads and the map's spawn data.
// A slot is 0 for the player, 1.. for the NPCs; a type is a character id (sh.chara_id("Name")).
//   npc.anim_duration(slot) / npc.anim_start_keyframe(slot): the active clip's duration (Q12 keyframes per second)
//       and first keyframe. npc.behavior_update(slot): one update of the NPC's Lua behaviour now.
//   npc.path_walk_no_wait(slot, anim, mode): one step along the event waypoints of slot 1 (sh.event_waypoint_set),
//       without advancing the event step on arrival.
//   npc.ride_state(slot) / npc.ride_horse(slot) / npc.set_ride_height(slot, y): the carousel words of the NPC block
//       (legs clip state, 12 = riding; the horse ridden; that horse's height, Q12).
//   npc.split_head_flags(slot): the Split Head's state flags. npc.record_reset(slot, type): the slot's record
//       cleared to an idle character of the type.
//   npc.spawn_position_set(type, spawn, x, z): the spawn point of the map's spawn row of that character.
//   npc.model_loaded(type): 1 when a model slot holds the type's model. npc.group_anim_chara(group): the character
//       whose animations the animation slot of chara group `group` holds.
//   map.chara_group_set(group, type): the character of chara group `group` of the loaded map.
//   map.spawn_info_flags_set(row, idx, flags): the spawn flags of entry idx of spawn row 0 or 1.
//   chara.update_variant_set(type, variant): the type's map update becomes its behavior.lua, variant 0 or 1.
//   chara.update_as(type, behaviour): the type runs its own behaviour or another type's (a boss playing another fight).
//   chara.anim_file_set(type, file): the animation file the type loads from now on.
//   chara.anim_data_alloc(anim_slot, type, buffer, bones): the type's animations into an animation slot from file
//       buffer `buffer`; the bones are the NPC bone buffer from an index or a bone array of the map's tables.
//   chara.model_load(type, model_slot, buffer, page, u, v, palette_x, palette_y): the type's model from a file
//       buffer ("lm:2": the second model buffer), its texture at the place given.
//   chara.model_loads_process(): the queued model loads finished.
//   load.tim_at(file, buffer, page, u, v, palette_x, palette_y): a TIM file through file buffer 0..2 onto the place.
//   load.loading_screen_image(file): a TIM file as the loading screen's background image.
//   player.map_anim_infos_set(first, table, count) / player.map_anim_ranges_set(first, array, count): Harry's map
//       clips from an animation table / his map keyframe ranges from an array of the loaded map's tables.

#include "engine/scripting/binds/binds.h"

#include <cstring>

#include "engine/chara/runtime/chara_models.h"
#include "engine/maps/map_runtime.h"
#include "engine/scripting/binds/game_links.h"

namespace sh::script::binds {

namespace {

using named::Call;
namespace links = sh::script::links;

constexpr int kCharaGroups = 4;  // the chara group words; animation slot group + 1 (slot 0 is Harry's)

bool slot_arg(Call& c, long long slot) {
    if (!links::chara_slot_valid(static_cast<int>(slot))) {
        c.fail("%s: bad character slot %lld", c.name(), slot);
        return false;
    }
    return true;
}

bool chara_type(long long type) { return type > 0 && type < links::kCharaTypes; }

int anim_clip(Call& c, bool duration) {
    if (!c.is_int(0)) {
        return c.fail("%s takes the character slot", c.name());
    }
    if (!slot_arg(c, c.integer(0))) {
        return -1;
    }
    std::int32_t length = 0;
    std::int32_t start = 0;
    if (!links::chara_active_clip(static_cast<int>(c.integer(0)), &length, &start)) {
        return c.fail("%s: the character has no clips", c.name());
    }
    return c.reply({duration ? length : start});
}

int anim_duration(Call& c) { return anim_clip(c, true); }
int anim_start_keyframe(Call& c) { return anim_clip(c, false); }

int behavior_update(Call& c) {
    if (!c.is_int(0)) {
        return c.fail("%s takes the character slot", c.name());
    }
    if (!slot_arg(c, c.integer(0))) {
        return -1;
    }
    if (c.integer(0) == 0) {
        return c.fail("%s: the player has no behaviour script", c.name());
    }
    links::chara_behavior_update(static_cast<int>(c.integer(0)));
    return 0;
}

int path_walk_no_wait(Call& c) {
    long long v[3];
    if (!c.ints(3, v) || !slot_arg(c, v[0])) {
        return -1;
    }
    links::chara_path_walk_no_wait(static_cast<int>(v[0]), static_cast<int>(v[1]), static_cast<int>(v[2]));
    return 0;
}

int ride_word(Call& c, bool state) {
    long long v[1];
    if (!c.ints(1, v) || !slot_arg(c, v[0])) {
        return -1;
    }
    const int slot = static_cast<int>(v[0]);
    return c.reply({state ? links::chara_ride_state(slot) : links::chara_ride_horse(slot)});
}

int ride_state(Call& c) { return ride_word(c, true); }
int ride_horse(Call& c) { return ride_word(c, false); }

int set_ride_height(Call& c) {
    long long v[2];
    if (!c.ints(2, v) || !slot_arg(c, v[0])) {
        return -1;
    }
    links::chara_set_ride_height(static_cast<int>(v[0]), static_cast<std::int32_t>(v[1]));
    return 0;
}

int split_head_flags(Call& c) {
    long long v[1];
    if (!c.ints(1, v) || !slot_arg(c, v[0])) {
        return -1;
    }
    return c.reply({links::chara_split_head_flags(static_cast<int>(v[0]))});
}

int record_reset(Call& c) {
    if (c.argc() != 2 || !c.ints_at(0, 2) || !links::chara_slot_valid(static_cast<int>(c.integer(0))) ||
        !chara_type(c.integer(1))) {
        return c.fail("%s: takes a character slot and a type", c.name());
    }
    links::chara_record_reset(static_cast<int>(c.integer(0)), static_cast<int>(c.integer(1)));
    return 0;
}

int spawn_position_set(Call& c) {
    if (c.argc() < 4 || !c.ints_at(0, 4)) {
        return c.fail("%s takes (chara id, spawn index, x, z) as integers", c.name());
    }
    if (c.integer(0) < 0 || c.integer(0) > 255) {
        return c.fail("%s: bad character id", c.name());
    }
    Chara_SpawnPositionSet(static_cast<int>(c.integer(0)), static_cast<int>(c.integer(1)),
                           static_cast<int>(c.integer(2)), static_cast<int>(c.integer(3)));
    return 0;
}

int model_loaded(Call& c) {
    long long v[1];
    if (!c.ints(1, v) || !c.in_range(v[0], 0, links::kCharaTypes - 1)) {
        return -1;
    }
    return c.reply({sh::chara::chara_models().group_has(static_cast<int>(v[0])) ? 1 : 0});
}

int group_anim_chara(Call& c) {
    long long v[1];
    if (!c.ints(1, v) || !c.in_range(v[0], 0, kCharaGroups - 2)) {
        return -1;
    }
    return c.reply({links::chara_group_anim_chara(static_cast<int>(v[0]))});
}

int chara_group_set(Call& c) {
    long long v[2];
    if (!c.ints(2, v) || !c.in_range(v[0], 0, sh::maps::kGroupCharas - 1) ||
        !c.in_range(v[1], -1, links::kCharaTypes - 1)) {
        return -1;
    }
    sh::maps::map_runtime().set_group_chara(static_cast<int>(v[0]), static_cast<int>(v[1]));
    return 0;
}

int spawn_info_flags_set(Call& c) {
    long long v[3];
    if (!c.ints(3, v)) {
        return -1;
    }
    if (v[0] < 0 || v[0] >= sh::maps::kSpawnGroups || v[1] < 0 || v[1] >= sh::maps::kSpawnSlots) {
        return c.fail("%s: row 0..%d and entry 0..%d expected", c.name(), sh::maps::kSpawnGroups - 1,
                      sh::maps::kSpawnSlots - 1);
    }
    sh::maps::map_runtime().spawn(static_cast<int>(v[0]), static_cast<int>(v[1])).flags =
        static_cast<std::int8_t>(static_cast<std::uint8_t>(v[2]));
    return 0;
}

int update_variant_set(Call& c) {
    if (c.argc() != 2 || !c.ints_at(0, 2)) {
        return c.fail("%s takes a character type and a variant", c.name());
    }
    const long long type = c.integer(0);
    const long long variant = c.integer(1);
    if (type < 0 || type >= links::kCharaTypes || (variant != 0 && variant != 1)) {
        return c.fail("%s: bad character type or variant (0 or 1)", c.name());
    }
    links::chara_update_set(static_cast<int>(type),
                            variant == 0 ? links::CharaUpdate::Scripted : links::CharaUpdate::ScriptedAlt);
    return 0;
}

int update_as(Call& c) {
    if (c.argc() != 2 || !c.ints_at(0, 2) || !chara_type(c.integer(0)) || !chara_type(c.integer(1))) {
        return c.fail("%s: takes two character types", c.name());
    }
    if (c.integer(1) == c.integer(0)) {
        links::chara_update_set(static_cast<int>(c.integer(0)), links::CharaUpdate::Scripted);
    } else {
        links::chara_update_set(static_cast<int>(c.integer(0)), links::CharaUpdate::ScriptedAs,
                                static_cast<int>(c.integer(1)));
    }
    return 0;
}

int anim_file_set(Call& c) {
    if (c.argc() != 2 || !c.ints_at(0, 2) || !chara_type(c.integer(0)) || c.integer(1) < 0) {
        return c.fail("%s: takes a character type and a file", c.name());
    }
    links::chara_anim_file_set(static_cast<int>(c.integer(0)), static_cast<int>(c.integer(1)));
    return 0;
}

// The file buffers the ending's character loads use: 26, 29..34, or "lm:2".
void* ending_buffer(Call& c, int i) {
    if (c.is_text(i)) {
        return std::strcmp(c.text(i), "lm:2") == 0 ? links::model_buffer_2() : nullptr;
    }
    if (!c.is_int(i)) {
        return nullptr;
    }
    const long long n = c.integer(i);
    return n == 26 || (n >= 29 && n <= 34) ? links::file_buffer(static_cast<int>(n)) : nullptr;
}

int anim_data_alloc(Call& c) {
    if (c.argc() != 4 || !c.ints_at(0, 2) || !chara_type(c.integer(1))) {
        return c.fail("%s: takes an animation slot, a character type, a buffer and the bones", c.name());
    }
    void* buffer = ending_buffer(c, 2);
    if (buffer == nullptr) {
        return c.fail("%s: unknown file buffer", c.name());
    }
    void* bones = c.is_int(3) ? links::npc_bones(static_cast<int>(c.integer(3)))
                  : c.is_text(3) ? MapTables_DataSymbol(c.text(3))
                                 : nullptr;
    if (bones == nullptr) {
        return c.fail("%s: the bones are an NPC bone index or a bone array of the map's tables", c.name());
    }
    links::chara_anim_data_alloc(static_cast<int>(c.integer(0)), static_cast<int>(c.integer(1)), buffer, bones);
    return 0;
}

int model_load(Call& c) {
    if (c.argc() != 8 || !c.ints_at(0, 2) || !c.ints_at(3, 5) || !chara_type(c.integer(0))) {
        return c.fail("%s: takes a type, a model slot, a buffer, a page, u, v and the palette's x, y", c.name());
    }
    void* buffer = ending_buffer(c, 2);
    if (buffer == nullptr) {
        return c.fail("%s: unknown file buffer", c.name());
    }
    links::chara_model_load(static_cast<int>(c.integer(0)), static_cast<int>(c.integer(1)), buffer,
                            static_cast<int>(c.integer(3)), static_cast<int>(c.integer(4)),
                            static_cast<int>(c.integer(5)), static_cast<int>(c.integer(6)),
                            static_cast<int>(c.integer(7)));
    return 0;
}

int model_loads_process(Call&) {
    sh::chara::chara_models().process_loads();
    return 0;
}

int tim_at(Call& c) {
    if (c.argc() != 7 || !c.ints_at(0, 7)) {
        return c.fail("%s takes a file, a buffer, a page, u, v and the palette's x and y", c.name());
    }
    const long long n = c.integer(1);
    void* buffer = n >= 0 && n <= 2 ? links::file_buffer(static_cast<int>(n)) : nullptr;
    if (buffer == nullptr) {
        return c.fail("%s: bad file buffer number", c.name());
    }
    return c.reply({links::queue_tim_at(static_cast<int>(c.integer(0)), buffer, static_cast<int>(c.integer(2)),
                                        static_cast<int>(c.integer(3)), static_cast<int>(c.integer(4)),
                                        static_cast<int>(c.integer(5)), static_cast<int>(c.integer(6)))});
}

int loading_screen_image(Call& c) {
    if (c.argc() != 1 || c.is_text(0)) {
        return c.fail("%s takes a file name", c.name());
    }
    return c.reply({links::queue_loading_screen_image(static_cast<int>(c.integer(0)))});
}

int map_anim_infos_set(Call& c) {
    if (c.argc() != 3 || !c.is_int(0) || !c.is_text(1) || !c.is_int(2)) {
        return c.fail("%s takes the first clip, a table name and a count", c.name());
    }
    switch (links::player_map_anim_infos_set(static_cast<int>(c.integer(0)), c.text(1), static_cast<int>(c.integer(2)))) {
        case 0: return 0;
        case 1: return c.fail("%s: unknown animation table", c.name());
        default: return c.fail("%s: bad range", c.name());
    }
}

int map_anim_ranges_set(Call& c) {
    if (c.argc() != 3 || !c.is_int(0) || !c.is_text(1) || !c.is_int(2)) {
        return c.fail("%s takes the first row, an array name and a count", c.name());
    }
    switch (links::player_map_anim_ranges_set(static_cast<int>(c.integer(0)), c.text(1), static_cast<int>(c.integer(2)))) {
        case 0: return 0;
        case 1: return c.fail("%s: the loaded map has no such array", c.name());
        default: return c.fail("%s: bad range", c.name());
    }
}

const named::Bind kBinds[] = {
    {"npc.anim_duration", anim_duration},
    {"npc.anim_start_keyframe", anim_start_keyframe},
    {"npc.behavior_update", behavior_update},
    {"npc.path_walk_no_wait", path_walk_no_wait},
    {"npc.ride_state", ride_state},
    {"npc.ride_horse", ride_horse},
    {"npc.set_ride_height", set_ride_height},
    {"npc.split_head_flags", split_head_flags},
    {"npc.record_reset", record_reset},
    {"npc.spawn_position_set", spawn_position_set},
    {"npc.model_loaded", model_loaded},
    {"npc.group_anim_chara", group_anim_chara},
    {"map.chara_group_set", chara_group_set},
    {"map.spawn_info_flags_set", spawn_info_flags_set},
    {"chara.update_variant_set", update_variant_set},
    {"chara.update_as", update_as},
    {"chara.anim_file_set", anim_file_set},
    {"chara.anim_data_alloc", anim_data_alloc},
    {"chara.model_load", model_load},
    {"chara.model_loads_process", model_loads_process},
    {"load.tim_at", tim_at},
    {"load.loading_screen_image", loading_screen_image},
    {"player.map_anim_infos_set", map_anim_infos_set},
    {"player.map_anim_ranges_set", map_anim_ranges_set},
};

}  // namespace

std::span<const named::Bind> chara_binds() { return kBinds; }

}  // namespace sh::script::binds
