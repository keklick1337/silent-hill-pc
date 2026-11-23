// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The host of the character behaviours in Lua: which
// gamedata/chara/<Name>/behavior.lua a slot runs, and the sh.entity.* calls those scripts make.
//
// The calls are served by extensions: a part of the game registers its record fields (entity.get / entity.set by
// readable name), its named functions and the clip tables of its types with one static Registrar. A field or
// function name is looked up in the base extension first, then in the others in registration order. A field no
// extension names is a behaviour variable of the slot (Character vars, chara/<Name>/state.json), answered by the
// Ports the game installs.
//
// Records, bones and clip tables are opaque pointers here; the game's extensions and ports are the character hosts
// (src/engine/chara/game).

#pragma once

#include "engine/chara/behavior_state.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct ShScriptArg;
struct ShScriptGameApi;

namespace sh::chara {
class CharaLibrary;
}

namespace sh::chara::behavior {

using Kind = FieldKind;

// A record field by readable name: `offset` bytes into the slot's record.
struct Field {
    const char* name;
    std::size_t offset;
    Kind kind;
};

// The arguments of a named call (sh.call conventions: kind 1 integer, 2 string, 3 boolean).
struct Args {
    int argc;
    const ShScriptArg* argv;
    long long integer(int i, long long fallback = 0) const;
    const char* text(int i) const;
};

// Returned by an extension's call for a name it does not serve.
constexpr int kNotMine = -2;

// `fn` is the name without "entity."; returns the count of values written to `out` (at most 8), or -1 with the
// message in `err`.
using Call = int (*)(std::string_view fn, const Args& a, long long* out, char* err, int errCap);

// A clip table a type plays from. A type may have several (per variant); the first is its default.
struct ClipTable {
    int chara_id;
    void* table;  // the game's clip array
    int count;
};

struct Extension {
    const Field* fields;
    std::size_t field_count;
    const char* const* names;  // "entity.<fn>"
    std::size_t name_count;
    Call call;
    const ClipTable* clips;
    std::size_t clip_count;
    Extension* next;
};

// Appends an extension; register_base_extension puts it before every other one.
void register_extension(Extension* ext);
void register_base_extension(Extension* ext);

struct Registrar {
    explicit Registrar(Extension* ext) { register_extension(ext); }
};

// What the host needs from the owner of the character records.
struct Ports {
    // The record of a slot as bytes for the field table (0 Harry, 1.. the NPCs, -1 the record the original read for
    // an unset NPC index), nullptr when none.
    std::uint8_t* (*record)(int slot) = nullptr;
    // After a field of  was written: slot -1 is a view, and this writes it back.
    void (*commit)(int slot) = nullptr;
    // The type of a slot's record, -1 when none.
    int (*chara_id)(int slot) = nullptr;
    // The slot's behaviour variables by name (its type's, else any type's).
    std::optional<long long> (*var_get)(int slot, std::string_view name) = nullptr;
    bool (*var_set)(int slot, std::string_view name, long long value) = nullptr;
    // The character data the behaviour names come from.
    const CharaLibrary* (*library)() = nullptr;
    // The type's clip table number `index` from the character data (asked before the extensions' tables).
    bool (*clip_table)(int chara_id, int index, ClipTable* out) = nullptr;
};
void install_ports(const Ports& ports);

// sh.entity.* for the scripts: chains the host in front of the API's named calls.
void bind_script_api(ShScriptGameApi* api);

// The type's clip table number `index` (the ports' first, then the extensions'); false past the last.
bool clip_table(int chara_id, int index, ClipTable* out);
// The table a record plays from: the one whose array is `active` when that is one of the type's tables, else the
// type's first; false when the type has none.
bool active_clip_table(int chara_id, const void* active, ClipTable* out);
bool has_clips(int chara_id);

// A field of the registered extensions (base first), nullptr when none names it.
const Field* find_field(std::string_view name);

int fail(char* err, int cap, const char* msg, const char* what = "");

// The behaviour folder a type runs: character.json "behavior" (types that run another type's update in every map
// header share its file), else its own folder. Empty for an unknown type.
std::string behaviour_name(const CharaLibrary& library, int chara_id);

// 1 when the type has a behaviour file.
bool behaviour_exists(int chara_id);

// One tick of the slot's behaviour as type `chara_id` (the slot's own type for -1). A `variant` >= 0 is what
// sh.entity.update_variant answers while it runs (1: the second update a map swaps in), 0 again afterwards; -1
// leaves the running variant as it is.
void run_behaviour(int slot, int chara_id = -1, int variant = -1);
// One tick of the generic fallback behaviour (chara/fallback_ai.lua).
void run_fallback(int slot);
int update_variant();

// What a slot runs each tick.
enum class SlotUpdate : std::uint8_t { None, Map, Behaviour, Fallback };
// What a spawn the map did not plan asked for (the engine's spawn modes).
enum class SpawnRun : std::uint8_t { Unset = 0, Map = 1, Own = 2, Fallback = 3, None = 4 };
// The choice for a slot of `chara_id`: a map-planned slot runs the map's update (None when the map has none); an
// engine spawn runs what its spawn chose, falling back from the map's update to the type's own behaviour and,
// for SpawnRun::Own, to the generic fallback when the type has clips. The behaviour and clip lookups happen only
// when the choice needs them.
SlotUpdate choose_update(int chara_id, bool map_has_update, bool engine_spawned, SpawnRun run);

}  // namespace sh::chara::behavior
