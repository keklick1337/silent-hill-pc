// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// What the modding tools' Lua editor knows about the code being typed: the
// names a running script state has (the engine's sh.* API, the Lua library, the scripts' own globals), the
// completions at the caret, the call the caret is in, the descriptions of the API read from
// gamedata/tools/lua_api.json, and the files "New script" writes. No UI, no Lua state, no game: the panel gives the
// listing of the live state as text and the unit tests drive the rest directly.

#pragma once

#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

namespace sh::devtools {

enum class LuaNameKind { Function, Table, Value };
// Engine: `sh` and everything under it. Library: what a plain Lua state has too. Script: the other globals of the
// running scripts (the host's helpers such as wait and say, the game script's own functions).
enum class LuaOrigin { Engine, Library, Script };

struct LuaName {
    std::string name;  // dotted from the globals: "sh.npc.spawn", "string.format", "pairs"
    LuaNameKind kind = LuaNameKind::Value;
    LuaOrigin origin = LuaOrigin::Script;
};

// The names of a script state, by dotted name.
class LuaNames {
public:
    void clear() { names_.clear(); }
    void add(std::string_view dotted, LuaNameKind kind, LuaOrigin origin);
    bool empty() const { return names_.empty(); }
    std::size_t size() const { return names_.size(); }
    const LuaName* find(std::string_view dotted) const;
    // The direct members of a table ("" the globals), in name order.
    std::vector<const LuaName*> members(std::string_view table) const;
    const std::map<std::string, LuaName, std::less<>>& all() const { return names_; }

private:
    std::map<std::string, LuaName, std::less<>> names_;
};

// A Lua expression that lists the globals of the state it runs in, tables walked three levels deep: one line per
// name, "f sh.npc.spawn", "t sh.npc", "v sh.NO_VALUE" (function, table, anything else).
std::string_view lua_names_snippet();

// Reads such a listing into `out` (added to what it has). A name `library` has is of the Lua library, a name
// under `sh` is the engine's, the rest are the scripts' own; `library` null reads the listing of a plain Lua state
// (everything is the library's).
void read_lua_names(std::string_view listing, const LuaNames* library, LuaNames& out);

// --- completion ---

struct LuaCompletionContext {
    bool valid = false;   // the caret is in code, right after an identifier or a '.'
    std::string table;    // "sh.npc" for "sh.npc.sp|"; "" for a bare word
    std::string partial;  // "sp"
    int begin = 0;        // where `partial` starts: a completion replaces [begin, caret)
    bool loose = false;   // after ':' or a '.' that follows a value, not a name: only the file's own words can follow
};

// What is being typed at byte offset `caret` of `text`. Not valid inside a string, a comment or a number.
LuaCompletionContext completion_context(std::string_view text, int caret);

enum class LuaCompletionKind { Keyword, Function, Table, Value, Word };

struct LuaCompletion {
    std::string text;  // what replaces the partial word
    std::string full;  // the dotted name ("sh.npc.spawn"): the key of its description
    LuaCompletionKind kind = LuaCompletionKind::Word;
    LuaOrigin origin = LuaOrigin::Script;
};

// The completions for `context`: members of the table from `names`, else the Lua keywords, the globals and the
// identifiers of the file (Word). Those starting with the partial word come first, then those containing it; the
// case does not matter for matching. At most `limit`.
std::vector<LuaCompletion> lua_completions(const LuaCompletionContext& context, const LuaNames& names,
                                           std::string_view text, int caret, std::size_t limit = 200);

// --- the call at the caret ---

struct LuaCallContext {
    std::string name;      // "sh.npc.spawn"; "" none
    bool in_call = false;  // the caret is inside the argument list (else on the name itself)
    int argument = 0;      // 0-based argument the caret is in
};

// The function whose argument list the caret is in (the innermost open parenthesis with a name before it), else
// the dotted name the caret touches.
LuaCallContext call_at(std::string_view text, int caret);

// --- descriptions of the API (gamedata/tools/lua_api.json) ---

struct LuaApiDoc {
    std::string name;       // "sh.give_item"
    std::string signature;  // "sh.give_item(item[, n])"
    std::string description;
};

class LuaApiDocs {
public:
    void clear() { docs_.clear(); }
    // {"functions": {"sh.name": {"signature": "...", "description": "..."}, ...}}: the entries are added to what
    // is there, an entry of the same name replacing the old one. An entry of the wrong shape is an error naming
    // it, and nothing of the text is taken.
    Result<void> parse(std::string_view json_text);
    const LuaApiDoc* find(std::string_view name) const;
    std::size_t size() const { return docs_.size(); }
    const std::map<std::string, LuaApiDoc, std::less<>>& all() const { return docs_; }

private:
    std::map<std::string, LuaApiDoc, std::less<>> docs_;
};

// `signature` written for another name of the same function: "sh.flag_get(flag)" as "sh.flag.get(flag)".
std::string signature_as(std::string_view signature, std::string_view from, std::string_view to);

// --- new scripts ---

struct NewScriptKind {
    std::string_view id;           // "map_events"
    std::string_view label;        // shown in the panel
    std::string_view subject;      // what else it needs: "" nothing, "map", "chara", "name"
    std::string_view description;  // where the file goes and when the game runs it
};

// The kinds of Lua file a mod may have.
std::span<const NewScriptKind> new_script_kinds();

struct NewScript {
    std::string relative;  // path in the mod: "maps/map1_s03/events.lua"
    std::string text;      // the starting text
};

// The file of `kind` for `subject` (a map name, a character name or a file name, as the kind asks) in mod `mod_id`.
Result<NewScript> new_script(std::string_view kind, std::string_view subject, std::string_view mod_id);

}  // namespace sh::devtools
