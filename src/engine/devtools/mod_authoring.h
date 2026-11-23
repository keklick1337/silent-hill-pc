// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The editor's mod authoring (docs/modding/editor.md): objects placed on a map — a trigger zone that shows a
// message, a trigger that runs a script, a character spawn, an item pickup — and the mod files they are saved to:
//
//   maps/<map>/editor_objects.json   the objects of one map (this format; the editor reads it back)
//   text/en/editor/<map>.json        the messages, new message slots of the map
//   flags/editor.json                the "done" and "taken" flags of the objects
//   scripts/main.lua                 the runtime, a generated region between two marker lines
//   maps/<map>/hooks.lua             the script objects' handlers (stubs the editor adds, the author edits)
//
// Everything here is text in, text out, so the format round-trips in the unit tests without a game.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

namespace sh::devtools {

enum class ModObjectKind : std::uint8_t { Message, Script, Spawn, Pickup };

const char* mod_object_kind_name(ModObjectKind kind);  // "message", "script", "spawn", "pickup"
bool parse_mod_object_kind(std::string_view text, ModObjectKind* kind);

// When a trigger fires: Harry walks into the zone, presses the action button inside it, or (spawns only) the map
// loads.
enum class ModActivation : std::uint8_t { Enter, Action, Load };
const char* mod_activation_name(ModActivation a);  // "enter", "action", "load"
bool parse_mod_activation(std::string_view text, ModActivation* a);

struct ModObject {
    std::string id;  // unique on the map: letters, digits, '_'
    ModObjectKind kind = ModObjectKind::Message;
    // Q12 like the whole API: 4096 = 1 m, y grows downwards; yaw a Q12 angle (4096 = a full turn).
    std::int32_t x = 0, y = 0, z = 0;
    std::int32_t yaw = 0;
    std::int32_t radius = 4096;  // the zone, a circle on the ground
    std::int32_t height = 8192;  // Harry's height may differ from y by this much (floors above and below)
    ModActivation activation = ModActivation::Enter;
    std::string flag;  // optional condition: the flag must have `flag_value`
    bool flag_value = true;
    bool once = false;        // fires once per playthrough (a "done" flag of the mod)
    std::string text;         // message: the English text
    std::string chara;        // spawn: a folder of chara/
    std::string ai = "auto";  // spawn: auto, full, fallback, none
    std::string item;         // pickup: an item name
    int count = 0;            // pickup: 0 is the game's common count
};

struct ModMapObjects {
    std::string map;
    std::vector<ModObject> objects;
};

bool valid_object_id(std::string_view id);

// maps/<map>/editor_objects.json.
std::string write_map_objects(const ModMapObjects& objects);
Result<ModMapObjects> parse_map_objects(std::string_view text);

// The names the generated files use. Flags are the mod's short names (flags/editor.json), the text key carries the
// mod's namespace, the script event is "<mod>:<object id>".
std::string object_text_key(std::string_view mod, std::string_view map, std::string_view id);
std::string object_done_flag(const ModObject& o);  // "" when the object needs none
std::string object_event(std::string_view mod, std::string_view id);

// text/en/editor/<map>.json; "" when the map has no message.
std::string write_text_doc(std::string_view mod, const ModMapObjects& objects);
// flags/editor.json for all the mod's maps; "" when no object needs a flag.
std::string write_flags_doc(std::string_view mod, const std::vector<ModMapObjects>& maps);
// The generated region of scripts/main.lua, the marker lines included.
std::string runtime_region(std::string_view mod, const std::vector<ModMapObjects>& maps);
// `existing` with its generated region replaced by `region` (or `region` added at the end; an empty file becomes the
// region alone).
std::string merge_main_lua(std::string_view existing, std::string_view region);
// `existing` hooks.lua of the map with a handler stub for the script object added; unchanged when it has one.
std::string add_script_stub(std::string_view existing, std::string_view mod, std::string_view map, std::string_view id);
// mod.json of a new mod for this engine version ("1.0.0"): scripts and gameplay on, the engine range from it.
std::string new_manifest(std::string_view id, std::string_view name, std::string_view engine_version);

}  // namespace sh::devtools
