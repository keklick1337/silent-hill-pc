// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/devtools/lua_assist.h"

#include <algorithm>
#include <array>
#include <cctype>

#include "engine/devtools/lua_edit.h"
#include "formats/common/json.h"

namespace sh::devtools {

namespace {

constexpr std::array<std::string_view, 22> kKeywords = {
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if",
    "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"};

bool ident_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// The dotted chain that ends right before `end` ("sh.npc" before the '.' of "sh.npc.sp"): identifiers joined by
// single dots, no spaces. "" when what is there is not a name (a call's result, a string, "..").
std::string chain_before(std::string_view text, std::size_t end) {
    std::size_t p = end;
    while (p > 0) {
        std::size_t q = p;
        while (q > 0 && ident_char(text[q - 1])) --q;
        if (q == p || !ident_start(text[q])) return "";
        p = q;
        if (p > 0 && text[p - 1] == '.' && !(p > 1 && text[p - 2] == '.')) {
            --p;
            continue;
        }
        break;
    }
    // A name that itself hangs off a value ("f().a.b") is not a chain from the globals.
    if (p > 0 && (text[p - 1] == ':' || text[p - 1] == ')' || text[p - 1] == ']')) return "";
    return std::string(text.substr(p, end - p));
}

struct Line {
    std::size_t begin = 0, end = 0;
    LuaLexState state;  // at the line's start
};

// Calls `fn(line)` for every line of `text`; stops when it returns false.
template <typename Fn>
void each_line(std::string_view text, Fn&& fn) {
    LuaLexState state;
    std::size_t b = 0;
    while (b <= text.size()) {
        std::size_t e = text.find('\n', b);
        if (e == std::string_view::npos) e = text.size();
        const Line line{b, e, state};
        if (!fn(line, state)) return;
        b = e + 1;
    }
}

// The identifiers of the file outside strings, comments and numbers: fn(begin, end, chain before its dot or "").
template <typename Fn>
void each_identifier(std::string_view text, Fn&& fn) {
    each_line(text, [&](const Line& line, LuaLexState& state) {
        const std::string_view s = text.substr(line.begin, line.end - line.begin);
        const std::vector<LuaSpan> spans = tokenize_lua_line(s, state);
        std::size_t next_span = 0;
        std::size_t i = 0;
        while (i < s.size()) {
            while (next_span < spans.size() && static_cast<std::size_t>(spans[next_span].end) <= i) ++next_span;
            if (next_span < spans.size() && static_cast<std::size_t>(spans[next_span].begin) <= i) {
                i = static_cast<std::size_t>(spans[next_span].end);
                continue;
            }
            if (!ident_start(s[i])) {
                ++i;
                continue;
            }
            std::size_t j = i + 1;
            while (j < s.size() && ident_char(s[j])) ++j;
            const bool dotted = i > 0 && s[i - 1] == '.' && !(i > 1 && s[i - 2] == '.');
            fn(line.begin + i, line.begin + j, dotted ? chain_before(text, line.begin + i - 1) : std::string());
            i = j;
        }
        return true;
    });
}

}  // namespace

void LuaNames::add(std::string_view dotted, LuaNameKind kind, LuaOrigin origin) {
    if (dotted.empty()) return;
    names_.insert_or_assign(std::string(dotted), LuaName{std::string(dotted), kind, origin});
}

const LuaName* LuaNames::find(std::string_view dotted) const {
    const auto it = names_.find(dotted);
    return it == names_.end() ? nullptr : &it->second;
}

std::vector<const LuaName*> LuaNames::members(std::string_view table) const {
    std::vector<const LuaName*> out;
    const std::string prefix = table.empty() ? std::string() : std::string(table) + ".";
    for (auto it = names_.lower_bound(prefix); it != names_.end(); ++it) {
        const std::string& n = it->first;
        if (n.compare(0, prefix.size(), prefix) != 0) break;
        if (n.find('.', prefix.size()) == std::string::npos) out.push_back(&it->second);
    }
    return out;
}

std::string_view lua_names_snippet() {
    return "(function() "
           "local out, seen = {}, {} "
           "local function walk(t, prefix, depth) "
           "seen[t] = true "
           "for k, v in pairs(t) do "
           "if type(k) == 'string' and k:match('^[%a_][%w_]*$') then "
           "local tv = type(v) "
           "out[#out + 1] = (tv == 'function' and 'f ' or tv == 'table' and 't ' or 'v ') .. prefix .. k "
           "if tv == 'table' and depth < 3 and not seen[v] then walk(v, prefix .. k .. '.', depth + 1) end "
           "end end end "
           "walk(_ENV, '', 0) "
           "return table.concat(out, '\\n') "
           "end)()";
}

void read_lua_names(std::string_view listing, const LuaNames* library, LuaNames& out) {
    std::size_t b = 0;
    while (b < listing.size()) {
        std::size_t e = listing.find('\n', b);
        if (e == std::string_view::npos) e = listing.size();
        const std::string_view line = listing.substr(b, e - b);
        b = e + 1;
        if (line.size() < 3 || line[1] != ' ' || (line[0] != 'f' && line[0] != 't' && line[0] != 'v')) continue;
        const std::string_view name = line.substr(2);
        if (!ident_start(name[0])) continue;
        const LuaNameKind kind = line[0] == 'f' ? LuaNameKind::Function : line[0] == 't' ? LuaNameKind::Table : LuaNameKind::Value;
        LuaOrigin origin = LuaOrigin::Script;
        if (library == nullptr || library->find(name) != nullptr) origin = LuaOrigin::Library;
        if (library != nullptr && (name == "sh" || name.rfind("sh.", 0) == 0)) origin = LuaOrigin::Engine;
        out.add(name, kind, origin);
    }
}

LuaCompletionContext completion_context(std::string_view text, int caret_in) {
    LuaCompletionContext ctx;
    const std::size_t caret = static_cast<std::size_t>(std::clamp(caret_in, 0, static_cast<int>(text.size())));
    bool in_code = true;
    each_line(text, [&](const Line& line, LuaLexState& state) {
        const std::string_view s = text.substr(line.begin, line.end - line.begin);
        const bool open_before = state.long_level >= 0;
        const std::vector<LuaSpan> spans = tokenize_lua_line(s, state);
        if (caret > line.end) return true;
        const std::size_t col = caret - line.begin;
        for (const LuaSpan& sp : spans) {
            if (sp.kind != LuaToken::String && sp.kind != LuaToken::Comment && sp.kind != LuaToken::Number) continue;
            const bool continued = open_before && sp.begin == 0;  // a long bracket still open from the line above
            if ((continued || col > static_cast<std::size_t>(sp.begin)) && col <= static_cast<std::size_t>(sp.end)) {
                // Right after a closed string or number the caret is in code again, but nothing is being typed.
                in_code = false;
            }
        }
        return false;
    });
    if (!in_code) return ctx;

    std::size_t b = caret;
    while (b > 0 && ident_char(text[b - 1])) --b;
    if (b < caret && !ident_start(text[b])) return ctx;
    ctx.partial = std::string(text.substr(b, caret - b));
    ctx.begin = static_cast<int>(b);
    const bool dot = b > 0 && text[b - 1] == '.' && !(b > 1 && text[b - 2] == '.');
    const bool colon = b > 0 && text[b - 1] == ':' && !(b > 1 && text[b - 2] == ':');
    if (dot) {
        ctx.table = chain_before(text, b - 1);
        ctx.loose = ctx.table.empty();
    } else if (colon) {
        ctx.loose = true;
    }
    ctx.valid = !ctx.partial.empty() || dot || colon;
    return ctx;
}

std::vector<LuaCompletion> lua_completions(const LuaCompletionContext& ctx, const LuaNames& names,
                                           std::string_view text, int caret, std::size_t limit) {
    std::vector<LuaCompletion> out;
    if (!ctx.valid) return out;
    (void)caret;
    std::map<std::string, LuaCompletion, std::less<>> found;
    auto add = [&](std::string_view word, std::string full, LuaCompletionKind kind, LuaOrigin origin) {
        found.try_emplace(std::string(word), LuaCompletion{std::string(word), std::move(full), kind, origin});
    };
    auto add_names = [&](std::string_view table) {
        for (const LuaName* n : names.members(table)) {
            const std::string_view word = std::string_view(n->name).substr(table.empty() ? 0 : table.size() + 1);
            add(word, n->name,
                n->kind == LuaNameKind::Function ? LuaCompletionKind::Function
                : n->kind == LuaNameKind::Table  ? LuaCompletionKind::Table
                                                 : LuaCompletionKind::Value,
                n->origin);
        }
    };
    if (!ctx.loose && !ctx.table.empty()) {
        add_names(ctx.table);
        each_identifier(text, [&](std::size_t b, std::size_t e, const std::string& chain) {
            if (static_cast<int>(b) != ctx.begin && chain == ctx.table)
                add(text.substr(b, e - b), ctx.table + "." + std::string(text.substr(b, e - b)), LuaCompletionKind::Word,
                    LuaOrigin::Script);
        });
    } else {
        if (!ctx.loose) {
            add_names("");
            for (std::string_view k : kKeywords) add(k, std::string(k), LuaCompletionKind::Keyword, LuaOrigin::Library);
        }
        each_identifier(text, [&](std::size_t b, std::size_t e, const std::string&) {
            const std::string_view word = text.substr(b, e - b);
            if (static_cast<int>(b) == ctx.begin) return;
            if (std::find(kKeywords.begin(), kKeywords.end(), word) != kKeywords.end()) return;
            add(word, std::string(word), LuaCompletionKind::Word, LuaOrigin::Script);
        });
    }

    const std::string want = lower(ctx.partial);
    struct Ranked {
        int rank;
        const LuaCompletion* c;
    };
    std::vector<Ranked> ranked;
    for (const auto& [word, c] : found) {
        int rank = 0;
        if (!ctx.partial.empty()) {
            const std::string low = lower(word);
            if (word.compare(0, ctx.partial.size(), ctx.partial) == 0) rank = 0;
            else if (low.compare(0, want.size(), want) == 0) rank = 1;
            else if (low.find(want) != std::string::npos) rank = 2;
            else continue;
        }
        ranked.push_back({rank, &c});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) { return a.rank < b.rank; });
    for (const Ranked& r : ranked) {
        if (out.size() >= limit) break;
        out.push_back(*r.c);
    }
    return out;
}

LuaCallContext call_at(std::string_view text, int caret_in) {
    LuaCallContext ctx;
    const std::size_t caret = static_cast<std::size_t>(std::clamp(caret_in, 0, static_cast<int>(text.size())));
    // The innermost open parenthesis before the caret that follows a name. A call rarely spans far: the search
    // stops after a few thousand bytes.
    const std::size_t floor = caret > 4000 ? caret - 4000 : 0;
    int depth = 0, commas = 0;
    for (std::size_t p = caret; p > floor; --p) {
        const char c = text[p - 1];
        if (c == ')' || c == '}' || c == ']') {
            ++depth;
        } else if (c == '{' || c == '[') {
            if (depth == 0) {
                commas = 0;  // inside a table or an index of the argument: the commas so far were its own
            } else {
                --depth;
            }
        } else if (c == ',' && depth == 0) {
            ++commas;
        } else if (c == '(') {
            if (depth > 0) {
                --depth;
                continue;
            }
            std::size_t e = p - 1;
            while (e > 0 && (text[e - 1] == ' ' || text[e - 1] == '\t')) --e;
            std::size_t b = e;
            while (b > 0 && (ident_char(text[b - 1]) || text[b - 1] == '.' || text[b - 1] == ':')) --b;
            const std::string_view name = text.substr(b, e - b);
            const bool keyword = std::find(kKeywords.begin(), kKeywords.end(), name) != kKeywords.end();
            if (!name.empty() && ident_start(name[0]) && !keyword) {
                ctx.name = std::string(name);
                ctx.in_call = true;
                ctx.argument = commas;
                return ctx;
            }
            commas = 0;  // a grouping parenthesis: keep looking outwards
        }
    }
    std::size_t b = caret, e = caret;
    while (b > 0 && (ident_char(text[b - 1]) || (text[b - 1] == '.' && b > 1 && ident_char(text[b - 2])))) --b;
    while (e < text.size() && ident_char(text[e])) ++e;
    if (e > b && ident_start(text[b])) ctx.name = std::string(text.substr(b, e - b));
    return ctx;
}

Result<void> LuaApiDocs::parse(std::string_view json_text) {
    auto parsed = json::parse(json_text);
    if (!parsed) return Result<void>::fail(parsed.error());
    const json::Value* functions = parsed.value().is_object() ? parsed.value().find("functions") : nullptr;
    if (functions == nullptr || !functions->is_object()) return Result<void>::fail("no \"functions\" object");
    std::map<std::string, LuaApiDoc, std::less<>> docs;
    for (const auto& [name, entry] : functions->members()) {
        if (!entry.is_object()) return Result<void>::fail("\"" + name + "\" is not an object");
        const json::Value* signature = entry.find("signature");
        const json::Value* description = entry.find("description");
        if (signature == nullptr || !signature->is_string())
            return Result<void>::fail("\"" + name + "\" has no \"signature\" string");
        if (description != nullptr && !description->is_string())
            return Result<void>::fail("\"" + name + "\": \"description\" is not a string");
        docs[name] = LuaApiDoc{name, signature->string(), description != nullptr ? description->string() : std::string()};
    }
    for (auto& [name, doc] : docs) docs_[name] = std::move(doc);
    return Result<void>::ok();
}

const LuaApiDoc* LuaApiDocs::find(std::string_view name) const {
    const auto it = docs_.find(name);
    return it == docs_.end() ? nullptr : &it->second;
}

std::string signature_as(std::string_view signature, std::string_view from, std::string_view to) {
    if (signature.rfind(from, 0) == 0 && (signature.size() == from.size() || !ident_char(signature[from.size()])))
        return std::string(to) + std::string(signature.substr(from.size()));
    return std::string(signature);
}

namespace {

constexpr std::array<NewScriptKind, 6> kNewScriptKinds = {{
    {"main", "Mod script (scripts/main.lua)", "",
     "The mod's own script: runs once when the scripts start, in the mod's own environment; on_boot, on_new_game, "
     "on_map_load and on_update are called by the engine. One per mod."},
    {"map_events", "Map events layer (maps/<map>/events.lua)", "map",
     "Runs after the game's events.lua of the map: wrap its handlers with extend() or add new ones."},
    {"map_hooks", "Map hooks layer (maps/<map>/hooks.lua)", "map",
     "Runs after the game's hooks.lua of the map (music and other engine hooks of the map)."},
    {"map_world_objects", "World objects layer (maps/<map>/world_objects.lua)", "map",
     "Runs after the game's world_objects.lua of the map: update(dt), pre_draw(), post_init() and the others."},
    {"chara_behavior", "Character behaviour layer (chara/<Name>/behavior.lua)", "chara",
     "Runs after the game's behavior.lua of the character: wrap update(slot) with extend()."},
    {"menu", "Menu (menus/<name>.lua)", "name",
     "A menu file: read with the other menus when the scripts start; sees only sh.menu, sh.config, sh.mods, sh.log."},
}};

bool plain_name(std::string_view s) {
    if (s.empty() || s.size() > 64) return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return ident_char(c); });
}

std::string filled(std::string_view pattern, std::string_view mod, std::string_view subject) {
    std::string out;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern.compare(i, 5, "<mod>") == 0) {
            out += mod;
            i += 4;
        } else if (pattern.compare(i, 3, "<s>") == 0) {
            out += subject;
            i += 2;
        } else {
            out.push_back(pattern[i]);
        }
    }
    return out;
}

constexpr std::string_view kMainText =
    "-- <mod>: the mod's own script. It runs once when the scripts start, in the mod's own environment (its globals\n"
    "-- are not seen by the game or by other mods). `mod` is this mod: mod.id, mod.name, mod.version, mod.dir and\n"
    "-- mod.storage, a table that lasts for the session.\n"
    "\n"
    "function on_boot()\n"
    "    sh.log(mod.id .. \": loaded\")\n"
    "end\n"
    "\n"
    "-- Engine events: any number of mods listen without replacing each other (the list: docs/modding/events.md).\n"
    "sh.hooks.on(\"map.load\", function(ev)\n"
    "    sh.log(mod.id .. \": map \" .. ev.map)\n"
    "end, { id = \"map_load\" })\n";

constexpr std::string_view kMapEventsText =
    "-- <mod>: a layer over maps/<s>/events.lua. It runs after the game's file, in its own environment that reads\n"
    "-- the game's globals. The handlers of the map are the \"name\" fields of maps/<s>/handlers.json.\n"
    "\n"
    "function on_boot()\n"
    "    sh.log(mod.id .. \": events layer of <s> loaded\")\n"
    "end\n"
    "\n"
    "-- Wrap a handler of the map (`base` is its definition before this layer), or define a new MapEvent_ function.\n"
    "-- extend(\"MapEvent_Name\", function(base, ...)\n"
    "--     return base(...)\n"
    "-- end)\n";

constexpr std::string_view kMapHooksText =
    "-- <mod>: a layer over maps/<s>/hooks.lua, the engine hooks of the map (scripts/map_hooks.lua runs before it in\n"
    "-- the same state). bgm(init) is the map's music event: init is 1 when the room's track is chosen, 0 every tick.\n"
    "\n"
    "-- extend(\"bgm\", function(base, init)\n"
    "--     return base(init)\n"
    "-- end)\n";

constexpr std::string_view kWorldObjectsText =
    "-- <mod>: a layer over maps/<s>/world_objects.lua (a new Lua state at every visit of the map). The engine calls\n"
    "-- the global functions update(dt) (dt in Q12 seconds), pre_draw(), post_draw(), post_init(), post_camera() and\n"
    "-- npc_spawn().\n"
    "\n"
    "-- extend(\"update\", function(base, dt)\n"
    "--     return base(dt)\n"
    "-- end)\n";

constexpr std::string_view kCharaText =
    "-- <mod>: a layer over chara/<s>/behavior.lua. The engine calls update(slot) of the character every tick; the\n"
    "-- slot is the character's number (sh.npc.*, sh.entity.*).\n"
    "\n"
    "-- extend(\"update\", function(base, slot)\n"
    "--     return base(slot)\n"
    "-- end)\n";

constexpr std::string_view kMenuText =
    "-- <mod>: a menu. The menu files of every mod share one Lua state with the base menus and see only sh.menu,\n"
    "-- sh.config, sh.mods and sh.log: keep the variables local.\n"
    "\n"
    "sh.menu.define{\n"
    "    id = \"<mod>:<s>\",\n"
    "    title = \"<s>\",\n"
    "    rows = {\n"
    "        { kind = \"info\", id = \"<mod>:<s>.about\", label = \"About\", text = function() return \"<mod>\" end },\n"
    "    },\n"
    "}\n"
    "\n"
    "-- The base pause menu is read before any mod's menus, so it exists here.\n"
    "if not sh.menu.append_rows(\"pause\", {\n"
    "    { kind = \"submenu\", id = \"<mod>:<s>.entry\", label = \"<s>\", submenu = \"<mod>:<s>\" },\n"
    "}) then\n"
    "    sh.log(\"<mod>: there is no pause menu to extend\")\n"
    "end\n";

}  // namespace

std::span<const NewScriptKind> new_script_kinds() { return kNewScriptKinds; }

Result<NewScript> new_script(std::string_view kind, std::string_view subject, std::string_view mod_id) {
    const auto it = std::find_if(kNewScriptKinds.begin(), kNewScriptKinds.end(),
                                 [&](const NewScriptKind& k) { return k.id == kind; });
    if (it == kNewScriptKinds.end()) return Result<NewScript>::fail("unknown script kind: " + std::string(kind));
    if (mod_id.empty()) return Result<NewScript>::fail("no mod id");
    if (!it->subject.empty() && !plain_name(subject))
        return Result<NewScript>::fail("the " + std::string(it->subject) + " name must be letters, digits and '_': '" +
                                       std::string(subject) + "'");
    std::string_view path, text;
    if (kind == "main") path = "scripts/main.lua", text = kMainText;
    else if (kind == "map_events") path = "maps/<s>/events.lua", text = kMapEventsText;
    else if (kind == "map_hooks") path = "maps/<s>/hooks.lua", text = kMapHooksText;
    else if (kind == "map_world_objects") path = "maps/<s>/world_objects.lua", text = kWorldObjectsText;
    else if (kind == "chara_behavior") path = "chara/<s>/behavior.lua", text = kCharaText;
    else path = "menus/<s>.lua", text = kMenuText;
    NewScript out{filled(path, mod_id, subject), filled(text, mod_id, subject)};
    if (!valid_lua_relative_path(out.relative)) return Result<NewScript>::fail("not a gamedata Lua path: " + out.relative);
    return Result<NewScript>::ok(std::move(out));
}

}  // namespace sh::devtools
