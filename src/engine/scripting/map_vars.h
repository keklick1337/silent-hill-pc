// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The map variables of the loaded map by name (sh.map.get/set and references by name). They are data: gamedata/config/map_vars.json (every map) and
// gamedata/maps/<map>/map_vars.json (the map's own, looked at first), each patched by the active mods:
//
//     {"vars": {"<name>": {<where>, "type": "u8", "count": 4, "offset": 0, "stride": 2, "layout": "vec3"}}}
//
// <where> is one of
//   "symbol":  "<name>"   storage the game code shares, found by name (the map's tables.json, the executable);
//   "object":  "<Name>"   the draw records of the map's world objects from that one on (world_objects.json order);
//   "storage": "<key>"    the engine's run-long block of that key (sh::maps::map_variables()); a symbol may name
//                         one too: used where the symbol is not found.
// "type" is s8 u8 s16 u16 s32 u32 (default s32) or ref (addressable, not a number); element i lies at
// offset + i * stride (stride defaults to the type's size). A "layout" names several variables at once:
//   vec3       name (ref), name.x/.y/.z (s32)
//   svec3      name (ref), name.x/.y/.z (s16)
//   pose       name, name.pos, name.rot (refs), name.pos.x/.y/.z (s32), name.rot.x/.y/.z (s16)
//   placement  name, name.pos (refs), name.pos.x/.y/.z (s32)
// on the world object draw records, whose layout the build gives (a pose stride for "object").

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace sh::scripting {

enum class MapVarType : std::uint8_t { S8, U8, S16, U16, S32, U32, Ref };

enum class MapVarLookup : std::uint8_t {
    Found,
    Unknown,  // no variable of this name on the loaded map
    Error,    // declared, but this element cannot be reached (the reason is in *why)
};

struct MapVarAddress {
    void* address = nullptr;
    MapVarType type = MapVarType::Ref;
};

// Element `index` of the variable `name` on the map `map` (the loaded one).
MapVarLookup map_variable(std::string_view map, std::string_view name, long long index, MapVarAddress* out,
                          std::string* why);

// Byte size of one element of a type (0 for ref).
constexpr int map_var_type_size(MapVarType t) {
    switch (t) {
        case MapVarType::S8:
        case MapVarType::U8: return 1;
        case MapVarType::S16:
        case MapVarType::U16: return 2;
        case MapVarType::S32:
        case MapVarType::U32: return 4;
        case MapVarType::Ref: break;
    }
    return 0;
}

}  // namespace sh::scripting
