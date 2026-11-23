// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/devtools/lua_ide.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "formats/common/json.h"

namespace sh::devtools {

namespace {

bool space(char c) { return c == ' ' || c == '\t'; }

std::string lua_quote(std::string_view s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out.push_back('\\');
        if (c == '\n') {
            out += "\\n";
            continue;
        }
        out.push_back(c);
    }
    return out + "\"";
}

}  // namespace

// --- signature help ---

LuaSignatureParts signature_parts(std::string_view sig) {
    LuaSignatureParts parts;
    std::size_t open = sig.find_first_of("({");
    if (open == std::string_view::npos) return parts;
    parts.table_call = sig[open] == '{';
    const char close_char = parts.table_call ? '}' : ')';
    // The argument list ends at the matching close; a default value may hold calls or tables of its own.
    int depth = 0;
    std::size_t close = sig.size();
    std::vector<std::size_t> commas;
    bool quoted = false;
    for (std::size_t i = open + 1; i < sig.size(); ++i) {
        const char c = sig[i];
        if (c == '"') quoted = !quoted;
        if (quoted) continue;
        if (c == '(' || c == '{') {
            ++depth;
        } else if ((c == ')' || c == '}') && depth > 0) {
            --depth;
        } else if (c == close_char && depth == 0) {
            close = i;
            break;
        } else if (c == ',' && depth == 0) {
            commas.push_back(i);
        }
    }
    std::size_t begin = open + 1;
    commas.push_back(close);
    for (const std::size_t end_at : commas) {
        std::size_t b = begin, e = end_at;
        while (b < e && (space(sig[b]) || sig[b] == '[' || sig[b] == ']')) ++b;
        while (e > b && (space(sig[e - 1]) || sig[e - 1] == '[' || sig[e - 1] == ']')) --e;
        if (e > b) parts.params.emplace_back(static_cast<int>(b), static_cast<int>(e));
        begin = end_at + 1;
    }
    if (!parts.params.empty()) {
        const auto [b, e] = parts.params.back();
        parts.variadic = sig.substr(static_cast<std::size_t>(b), static_cast<std::size_t>(e - b)) == "...";
    }
    return parts;
}

int active_parameter(const LuaSignatureParts& parts, int argument) {
    if (parts.table_call || parts.params.empty() || argument < 0) return -1;
    const int n = static_cast<int>(parts.params.size());
    if (argument < n) return argument;
    return parts.variadic ? n - 1 : -1;
}

// --- tests ---

std::string_view lua_test_harness() {
    // `env` is the test file's _ENV (the runner sets it as the chunk's first upvalue): what the file defines stays
    // out of the state's globals, and the state's globals (sh.*, the host's helpers) are seen through it.
    return R"lua(
local tests = {}
local env = setmetatable({}, {__index = _G})
local function one_line(e) return (tostring(e):gsub("[\r\n]+", " ")) end
function env.test(name, fn) tests[#tests + 1] = {name = tostring(name), fn = fn} end
function env.expect(cond, msg)
    if not cond then error(msg or "expectation failed", 2) end
    return cond
end
function env.expect_eq(got, want, msg)
    if got ~= want then
        error((msg and (tostring(msg) .. ": ") or "") .. "expected " .. tostring(want) .. ", got " .. tostring(got), 2)
    end
end
function env.expect_error(fn, msg)
    if pcall(fn) then error(msg or "expected an error", 2) end
end
local function run(chunk)
    local ok, e = pcall(chunk)
    if not ok then return "SHTEST ERROR\t" .. one_line(e) end
    local named = {}
    for k, v in pairs(env) do
        if type(k) == "string" and k:match("^test_") and type(v) == "function" then named[#named + 1] = k end
    end
    table.sort(named)
    for _, k in ipairs(named) do tests[#tests + 1] = {name = k, fn = env[k]} end
    local out = {}
    for _, t in ipairs(tests) do
        local passed, err = pcall(t.fn)
        out[#out + 1] = passed and ("SHTEST PASS\t" .. t.name) or ("SHTEST FAIL\t" .. t.name .. "\t" .. one_line(err))
    end
    out[#out + 1] = "SHTEST END"
    return table.concat(out, "\n")
end
return env, run
)lua";
}

int LuaTestReport::passed() const {
    return static_cast<int>(std::count_if(tests.begin(), tests.end(), [](const LuaTestResult& t) { return t.passed; }));
}

int LuaTestReport::failed() const { return static_cast<int>(tests.size()) - passed(); }

LuaTestReport parse_lua_test_output(std::string_view out) {
    LuaTestReport r;
    bool ended = false;
    std::size_t at = 0;
    while (at <= out.size()) {
        std::size_t nl = out.find('\n', at);
        if (nl == std::string_view::npos) nl = out.size();
        const std::string_view line = out.substr(at, nl - at);
        at = nl + 1;
        if (line.rfind("SHTEST ", 0) != 0) {
            if (!line.empty() || at <= out.size()) r.output.emplace_back(line);
            continue;
        }
        const std::string_view rest = line.substr(7);
        const std::size_t tab = rest.find('\t');
        const std::string_view tag = rest.substr(0, tab);
        const std::string_view tail = tab == std::string_view::npos ? std::string_view() : rest.substr(tab + 1);
        if (tag == "END") {
            ended = true;
        } else if (tag == "ERROR") {
            r.error = std::string(tail);
        } else if (tag == "PASS" || tag == "FAIL") {
            const std::size_t t2 = tail.find('\t');
            LuaTestResult t;
            t.name = std::string(tail.substr(0, t2));
            t.passed = tag == "PASS";
            if (t2 != std::string_view::npos) t.message = std::string(tail.substr(t2 + 1));
            r.tests.push_back(std::move(t));
        }
    }
    while (!r.output.empty() && r.output.back().empty()) r.output.pop_back();
    if (!ended && r.error.empty()) r.error = r.output.empty() ? "the test run gave no report" : r.output.back();
    return r;
}

// --- map variables ---

bool read_map_var_rows(std::string_view json_text, std::vector<MapVarRow>& rows) {
    auto parsed = json::parse(json_text);
    if (!parsed || !parsed.value().is_object()) return false;
    const json::Value* vars = parsed.value().find("vars");
    if (vars == nullptr || !vars->is_object()) return false;
    for (const auto& [name, v] : vars->members()) {
        if (!v.is_object()) continue;
        if (std::any_of(rows.begin(), rows.end(), [&](const MapVarRow& r) { return r.name == name; })) continue;
        MapVarRow row;
        row.name = name;
        if (const json::Value* t = v.find("type"); t != nullptr && t->is_string()) row.type = t->string();
        if (const json::Value* c = v.find("count"); c != nullptr && c->is_integer())
            row.count = static_cast<int>(std::max<std::int64_t>(1, c->integer()));
        for (const char* key : {"symbol", "object", "storage"}) {
            if (const json::Value* w = v.find(key); w != nullptr && w->is_string()) {
                row.where = std::string(key) + " " + w->string();
                break;
            }
        }
        rows.push_back(std::move(row));
    }
    return true;
}

std::vector<std::string> map_var_keys(const std::vector<MapVarRow>& rows, int max_elements) {
    std::vector<std::string> keys;
    for (const MapVarRow& r : rows) {
        if (r.count <= 1) {
            keys.push_back(r.name);
            continue;
        }
        for (int i = 0; i < std::min(r.count, max_elements); ++i)
            keys.push_back(r.name + "[" + std::to_string(i) + "]");
    }
    return keys;
}

namespace {

// "name[3]" -> `"name", 3`; "name" -> `"name"`.
std::string key_arguments(const std::string& key) {
    const std::size_t lb = key.find('[');
    if (lb == std::string::npos || key.back() != ']') return lua_quote(key);
    return lua_quote(key.substr(0, lb)) + ", " + key.substr(lb + 1, key.size() - lb - 2);
}

}  // namespace

std::string map_var_read_snippet(const std::vector<std::string>& keys) {
    std::string s =
        "local r = {}\nlocal function g(...) local ok, v = pcall(sh.map.get, ...) r[#r + 1] = ok and "
        "tostring(v) or \"?\" end\n";
    for (const std::string& k : keys) s += "g(" + key_arguments(k) + ")\n";
    return s + "return table.concat(r, \"\\n\")";
}

std::vector<std::string> parse_map_var_values(std::string_view out, std::size_t count) {
    std::vector<std::string> values;
    std::size_t at = 0;
    while (values.size() < count && at <= out.size()) {
        std::size_t nl = out.find('\n', at);
        if (nl == std::string_view::npos) nl = out.size();
        values.emplace_back(out.substr(at, nl - at));
        at = nl + 1;
    }
    values.resize(count, "?");
    return values;
}

std::string map_var_write_snippet(const std::string& key, std::string_view value_in) {
    std::string value(value_in);
    while (!value.empty() && space(value.back())) value.pop_back();
    while (!value.empty() && space(value.front())) value.erase(value.begin());
    if (value != "true" && value != "false") {
        if (value.empty()) return "";
        char* end = nullptr;
        (void)std::strtod(value.c_str(), &end);
        if (end == nullptr || *end != '\0') return "";
    }
    // sh.map.set(name, value[, i]): the element index follows the value.
    const std::size_t lb = key.find('[');
    if (lb == std::string::npos || key.back() != ']') return "sh.map.set(" + lua_quote(key) + ", " + value + ")";
    return "sh.map.set(" + lua_quote(key.substr(0, lb)) + ", " + value + ", " +
           key.substr(lb + 1, key.size() - lb - 2) + ")";
}

}  // namespace sh::devtools
