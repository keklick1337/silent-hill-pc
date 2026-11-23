// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/devtools/lua_edit.h"

#include <algorithm>
#include <array>
#include <cctype>

#include "engine/devtools/lua_assist.h"
#include "formats/common/json.h"

namespace sh::devtools {

namespace {

constexpr std::array<std::string_view, 22> kKeywords = {
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if",
    "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"};

bool ident_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// "[", "=" * level, "[" at `i`: the level, or -1 when it is not a long bracket.
int long_open_level(std::string_view s, std::size_t i) {
    if (i >= s.size() || s[i] != '[') return -1;
    std::size_t j = i + 1;
    while (j < s.size() && s[j] == '=') ++j;
    return j < s.size() && s[j] == '[' ? static_cast<int>(j - i - 1) : -1;
}

// The end (one past) of "]", "=" * level, "]" from `i`, or npos.
std::size_t long_close_end(std::string_view s, std::size_t i, int level) {
    for (std::size_t p = i; p < s.size(); ++p) {
        if (s[p] != ']') continue;
        std::size_t j = p + 1;
        int eq = 0;
        while (j < s.size() && s[j] == '=') ++j, ++eq;
        if (eq == level && j < s.size() && s[j] == ']') return j + 1;
    }
    return std::string_view::npos;
}

}  // namespace

std::vector<LuaSpan> tokenize_lua_line(std::string_view s, LuaLexState& state, const LuaNames* names) {
    std::vector<LuaSpan> out;
    std::size_t i = 0;
    auto push = [&](std::size_t b, std::size_t e, LuaToken k) {
        out.push_back({static_cast<int>(b), static_cast<int>(e), k});
    };
    if (state.long_level >= 0) {
        const LuaToken kind = state.long_comment ? LuaToken::Comment : LuaToken::String;
        const std::size_t e = long_close_end(s, 0, state.long_level);
        if (e == std::string_view::npos) {
            if (!s.empty()) push(0, s.size(), kind);
            return out;
        }
        push(0, e, kind);
        state = {};
        i = e;
    }
    while (i < s.size()) {
        const char c = s[i];
        if (c == '-' && i + 1 < s.size() && s[i + 1] == '-') {
            const int level = long_open_level(s, i + 2);
            if (level >= 0) {
                const std::size_t e = long_close_end(s, i + 4 + level, level);
                if (e == std::string_view::npos) {
                    push(i, s.size(), LuaToken::Comment);
                    state.long_level = level;
                    state.long_comment = true;
                    return out;
                }
                push(i, e, LuaToken::Comment);
                i = e;
                continue;
            }
            push(i, s.size(), LuaToken::Comment);
            return out;
        }
        if (c == '[') {
            const int level = long_open_level(s, i);
            if (level >= 0) {
                const std::size_t e = long_close_end(s, i + 2 + level, level);
                if (e == std::string_view::npos) {
                    push(i, s.size(), LuaToken::String);
                    state.long_level = level;
                    state.long_comment = false;
                    return out;
                }
                push(i, e, LuaToken::String);
                i = e;
                continue;
            }
        }
        if (c == '"' || c == '\'') {
            std::size_t j = i + 1;
            while (j < s.size() && s[j] != c) j += s[j] == '\\' ? 2 : 1;
            j = std::min(j + 1, s.size());
            push(i, j, LuaToken::String);
            i = j;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1])))) {
            std::size_t j = i;
            const bool hex = c == '0' && i + 1 < s.size() && (s[i + 1] == 'x' || s[i + 1] == 'X');
            if (hex) j += 2;
            while (j < s.size()) {
                const char d = s[j];
                const bool exp = hex ? (d == 'p' || d == 'P') : (d == 'e' || d == 'E');
                if (exp && j + 1 < s.size() && (s[j + 1] == '+' || s[j + 1] == '-')) {
                    j += 2;
                } else if (std::isalnum(static_cast<unsigned char>(d)) || d == '.') {
                    ++j;
                } else {
                    break;
                }
            }
            push(i, j, LuaToken::Number);
            i = j;
            continue;
        }
        if (ident_start(c)) {
            std::size_t j = i + 1;
            while (j < s.size() && ident_char(s[j])) ++j;
            const std::string_view word = s.substr(i, j - i);
            if (std::find(kKeywords.begin(), kKeywords.end(), word) != kKeywords.end()) {
                push(i, j, LuaToken::Keyword);
                i = j;
                if (word == "function") {
                    // The name being defined: "function a.b:c(", "local function f(".
                    std::size_t b = i;
                    while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) ++b;
                    std::size_t e = b;
                    while (e < s.size() && (ident_char(s[e]) || ((s[e] == '.' || s[e] == ':') && e + 1 < s.size() &&
                                                                 ident_start(s[e + 1]))))
                        ++e;
                    if (e > b && ident_start(s[b])) {
                        push(b, e, LuaToken::FunctionDef);
                        i = e;
                    }
                }
                continue;
            }
            // The chain of names from here: "sh.npc.spawn", "obj.field:method".
            const bool member = i > 0 && ((s[i - 1] == '.' && !(i > 1 && s[i - 2] == '.')) || s[i - 1] == ':');
            std::vector<std::pair<std::size_t, std::size_t>> segs{{i, j}};
            bool method = false;
            std::size_t end = j;
            while (!method && end + 1 < s.size() && (s[end] == '.' || s[end] == ':') && ident_start(s[end + 1])) {
                method = s[end] == ':';
                std::size_t e = end + 2;
                while (e < s.size() && ident_char(s[e])) ++e;
                segs.emplace_back(end + 1, e);
                end = e;
            }
            std::size_t after = end;
            while (after < s.size() && (s[after] == ' ' || s[after] == '\t')) ++after;
            const bool call = after < s.size() && (s[after] == '(' || s[after] == '"' || s[after] == '\'' ||
                                                   s[after] == '{' || long_open_level(s, after) >= 0);
            // The longest leading part the running state knows.
            std::size_t known = 0;
            const LuaName* known_name = nullptr;
            if (names != nullptr && !member) {
                const std::size_t dotted = method ? segs.size() - 1 : segs.size();
                for (std::size_t n = 0; n < dotted; ++n) {
                    const LuaName* found = names->find(s.substr(i, segs[n].second - i));
                    if (found == nullptr || found->origin == LuaOrigin::Script) break;
                    known = n + 1;
                    known_name = found;
                }
            }
            std::size_t plain = 0;  // the first segment not coloured yet
            if (known > 0) {
                push(i, segs[known - 1].second,
                     known_name->origin == LuaOrigin::Engine ? LuaToken::Api : LuaToken::Library);
                plain = known;
                const bool dot_member = known < segs.size() && !(method && known == segs.size() - 1);
                if (dot_member && known_name->origin == LuaOrigin::Engine && known_name->kind == LuaNameKind::Table) {
                    push(segs[known].first, segs[known].second, LuaToken::ApiUnknown);
                    plain = known + 1;
                }
            }
            if (call && plain < segs.size()) push(segs.back().first, segs.back().second, LuaToken::Call);
            i = end;
            continue;
        }
        ++i;
    }
    return out;
}

int lua_error_line(std::string_view m) {
    // The chunk name ends at the first ':' followed by digits and ':' (a quoted [string "..."] may hold colons).
    std::size_t from = 0;
    if (m.rfind("[string \"", 0) == 0) {
        const std::size_t close = m.find("\"]");
        if (close != std::string_view::npos) from = close + 2;
    }
    for (std::size_t p = m.find(':', from); p != std::string_view::npos; p = m.find(':', p + 1)) {
        std::size_t j = p + 1;
        int line = 0;
        while (j < m.size() && std::isdigit(static_cast<unsigned char>(m[j]))) line = line * 10 + (m[j++] - '0');
        if (j > p + 1 && j < m.size() && m[j] == ':') return line;
    }
    return 0;
}

bool valid_lua_relative_path(std::string_view rel) {
    if (rel.size() < 5 || rel.substr(rel.size() - 4) != ".lua") return false;
    if (rel.front() == '/' || rel.find('\\') != std::string_view::npos || rel.find(':') != std::string_view::npos)
        return false;
    std::size_t start = 0;
    while (start <= rel.size()) {
        std::size_t slash = rel.find('/', start);
        if (slash == std::string_view::npos) slash = rel.size();
        const std::string_view part = rel.substr(start, slash - start);
        if (part.empty() || part == "." || part == "..") return false;
        start = slash + 1;
    }
    return true;
}

Result<std::filesystem::path> mod_file_for(const std::filesystem::path& mod_root, std::string_view relative) {
    if (mod_root.empty()) return Result<std::filesystem::path>::fail("no mod folder");
    if (!valid_lua_relative_path(relative))
        return Result<std::filesystem::path>::fail("not a gamedata Lua path: " + std::string(relative));
    return Result<std::filesystem::path>::ok(mod_root / std::filesystem::path(std::string(relative)));
}

std::string new_mod_manifest(std::string_view id) {
    json::Value m = json::Value::empty_object();
    m.set("format", json::Value::from_integer(1));
    m.set("id", json::Value::from_string(std::string(id)));
    m.set("name", json::Value::from_string(std::string(id)));
    m.set("version", json::Value::from_string("1.0.0"));
    m.set("description", json::Value::from_string("Lua edits made with the built-in Lua editor"));
    m.set("scripts", json::Value::from_bool(true));
    return json::write(m, true) + "\n";
}

Result<std::string> manifest_for_saved_lua(std::string_view text, std::string_view relative, bool replace_whole,
                                           bool* changed) {
    *changed = false;
    auto parsed = json::parse(text);
    if (!parsed || !parsed.value().is_object()) return Result<std::string>::fail("mod.json is not a JSON object");
    json::Value m = parsed.value();
    const json::Value* scripts = m.find("scripts");
    if (scripts == nullptr || !scripts->is_bool() || !scripts->boolean()) {
        m.set("scripts", json::Value::from_bool(true));
        *changed = true;
    }
    if (replace_whole) {
        json::Value* list = m.find_mut("replace");
        if (list != nullptr && !list->is_array()) return Result<std::string>::fail("mod.json \"replace\" is not a list");
        if (list == nullptr) list = &m.set("replace", json::Value::empty_array());
        auto same = [&](const json::Value& v) {
            if (!v.is_string() || v.string().size() != relative.size()) return false;
            for (std::size_t i = 0; i < relative.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(v.string()[i])) !=
                    std::tolower(static_cast<unsigned char>(relative[i])))
                    return false;
            return true;
        };
        if (std::none_of(list->array().begin(), list->array().end(), same)) {
            list->array_mut().push_back(json::Value::from_string(std::string(relative)));
            *changed = true;
        }
    }
    if (!*changed) return Result<std::string>::ok(std::string(text));
    return Result<std::string>::ok(json::write(m, true) + "\n");
}

}  // namespace sh::devtools
