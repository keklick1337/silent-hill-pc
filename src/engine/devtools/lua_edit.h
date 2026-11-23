// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The pure parts of the modding tools' Lua editor: the line tokenizer the
// highlighter draws with, the line of a Lua error message, and where "Save to mod" puts a file and what it does to
// the mod's mod.json. No UI, no Lua state, no game: the unit tests drive these directly.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

namespace sh::devtools {

// Api: a name the engine's `sh` table has ("sh.npc.spawn"); ApiUnknown: a member of an `sh` table that the running
// state does not have; Library: a name of the Lua library ("string.format", "pairs"); FunctionDef: the name after
// `function`; Call: any other name that is called.
enum class LuaToken { Text, Keyword, String, Comment, Number, Api, ApiUnknown, Library, FunctionDef, Call };

class LuaNames;  // lua_assist.h

struct LuaSpan {
    int begin = 0;  // byte offsets in the line, [begin, end)
    int end = 0;
    LuaToken kind = LuaToken::Text;
};

// What a line leaves open for the next one: a long string or long comment ([[ ]], [==[ ]==]) of `level`.
struct LuaLexState {
    int long_level = -1;  // -1 none
    bool long_comment = false;
};

// The coloured spans of one line (plain text between them is not listed). `state` carries a long bracket across
// lines: pass the state the previous line left, starting from a default one. With `names` (the names of a running
// script state) the engine API and the Lua library are told apart; without it names are only calls or definitions.
std::vector<LuaSpan> tokenize_lua_line(std::string_view line, LuaLexState& state, const LuaNames* names = nullptr);

// The line a Lua compile or run error points at ("[string \"x\"]:12: ...", "snippet:3: ...", "file.lua:7: ..."),
// 0 when the message names none.
int lua_error_line(std::string_view message);

// A path of gamedata/ ("maps/map0_s01/events.lua") a Lua file may be saved under in a mod: relative, '/' only, no
// "." or ".." parts, ending in .lua.
bool valid_lua_relative_path(std::string_view relative);

// The file "Save to mod" writes: <mod_root>/<relative>.
Result<std::filesystem::path> mod_file_for(const std::filesystem::path& mod_root, std::string_view relative);

// The mod.json of a mod the editor creates: format 1, the id, version 1.0.0, "scripts": true.
std::string new_mod_manifest(std::string_view id);

// The mod.json text after saving `relative` into the mod: "scripts": true (a mod's Lua runs only with it) and,
// with `replace_whole`, the path in "replace" (the mod's copy then runs instead of the base file, not as an
// extension after it). The text comes back unchanged when nothing is missing;
// `changed` says whether it did change.
Result<std::string> manifest_for_saved_lua(std::string_view manifest_text, std::string_view relative,
                                           bool replace_whole, bool* changed);

}  // namespace sh::devtools
