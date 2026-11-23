// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The parts of the modding tools' Lua editor and flags panel that need no UI and no game (
// "Lua editor", "Flags"): the parameters of an API signature for the signature help, the harness and the report of
// a mod's Lua tests (docs/modding/scripting.md, "Tests"), and the map variables of map_vars.json with the Lua that
// reads their values.

#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sh::devtools {

// --- signature help ---

struct LuaSignatureParts {
    std::vector<std::pair<int, int>> params;  // [begin, end) of each parameter name in the signature
    bool table_call = false;  // "sh.render.sprite{texture, ...}": the fields of one table, not positional arguments
    bool variadic = false;    // the last parameter is "...": every further argument is it
};

// The parameters of "sh.give_item(item[, n]) -> count": "item" and "n". Brackets of optional parameters and the
// spaces around a parameter are not part of it; a comma inside a table or a call of a default stays in its
// parameter. A signature without an argument list has no parameters.
LuaSignatureParts signature_parts(std::string_view signature);

// The parameter of `parts` the 0-based `argument` is, -1 none (past the last of a non-variadic list, a table call).
int active_parameter(const LuaSignatureParts& parts, int argument);

// --- tests of a mod (tests/*.lua) ---

// The Lua the runner calls with a test file's compiled chunk: it returns the table the chunk runs in (the globals
// fall through to the state's) and the function that runs the chunk, then each test, and returns the report.
std::string_view lua_test_harness();

struct LuaTestResult {
    std::string name;  // "test_spawn" or the name given to test()
    bool passed = false;
    std::string message;  // the error of a failed test
};

struct LuaTestReport {
    std::vector<LuaTestResult> tests;
    std::string error;                // the file did not compile or its top level failed: no test ran
    std::vector<std::string> output;  // what the file and its tests printed
    int passed() const;
    int failed() const;
};

// Reads the text a run of the harness gives (the printed lines, then its report lines).
LuaTestReport parse_lua_test_output(std::string_view out);

// --- map variables (maps/<map>/map_vars.json, config/map_vars.json) ---

struct MapVarRow {
    std::string name;
    std::string type = "s32";  // s8..u32, ref
    int count = 1;
    std::string where;  // "symbol X", "storage X", "object X"
};

// The "vars" of one map_vars.json added to `rows` (a name already there is kept: the map's file is read first).
// False when the text is not such a file.
bool read_map_var_rows(std::string_view json_text, std::vector<MapVarRow>& rows);

// Lua that returns the value of every row ("name" or "name[i]" for the first `max_elements` of an array), one
// line each, "?" for one that cannot be read; parse_map_var_values splits that text back.
std::vector<std::string> map_var_keys(const std::vector<MapVarRow>& rows, int max_elements);
std::string map_var_read_snippet(const std::vector<std::string>& keys);
std::vector<std::string> parse_map_var_values(std::string_view out, std::size_t count);
// Lua that writes `value` (a number, true/false) to the key; "" when the value is not one.
std::string map_var_write_snippet(const std::string& key, std::string_view value);

}  // namespace sh::devtools
