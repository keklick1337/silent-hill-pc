// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The modding tools' Lua editor: a browser of the Lua files the game sees (the
// base gamedata and the active mods, with the layer each copy comes from), an editor with line numbers, Lua
// highlighting (the engine API and the Lua library by the names of the running script state), completion and the
// description of the function at the caret, a searchable reference of the API, find, go to line and undo, saving
// into a mod and new scripts in a mod (the install's base data is never written), the script reloads, a Lua line,
// the open file or its selection run in the game's or the map's script state, the signature help over the caret
// and a mod's tests/*.lua run with a pass/fail list. Console `luaedit` drives it for scripted runs.
//
// The highlighter draws the coloured text itself under an InputTextMultiline whose own text is transparent: the
// widget keeps the editing (cursor, selection, clipboard, its own Ctrl+Z), the panel only paints.
//
// Layout: the panel's window never scrolls. Everything is in one child of the window's size; the file list, the
// API list and the code take the height that is left and are the only regions the wheel scrolls.

#include "features/tools/tools.h"
#include "features/tools/tools_internal.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "engine/console/instance.h"
#include "engine/console/registry.h"
#include "engine/devtools/lua_assist.h"
#include "engine/devtools/lua_edit.h"
#include "engine/devtools/lua_ide.h"
#include "engine/devtools/position_snippets.h"
#include "engine/game_state/game_state.h"
#include "engine/log/sh_log.h"
#include "engine/mods/manifest.h"
#include "engine/mods/mods.h"
#include "engine/mods/registry.h"

#if defined(SH_LUA)
#include "engine/script/api_registry.h"
#include "engine/script/sh_script.h"
// The vendored lua.h has no extern "C" guard of its own.
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
#endif

extern SDL_Window* g_window;

namespace sh::tools {

namespace {

namespace dt = sh::devtools;
namespace fs = std::filesystem;
namespace gs = sh::game_state;

// --- the log lines about Lua files (warnings and errors), collected from any thread ---

std::mutex& log_mutex() {
    static std::mutex m;
    return m;
}
std::deque<std::string>& log_lines() {
    static std::deque<std::string> lines;
    return lines;
}

void log_listener(int, int level, const char* line) {
    if (level > SH_LOGLV_WARN || line == nullptr) return;
    if (std::strstr(line, ".lua") == nullptr) return;
    std::lock_guard lock(log_mutex());
    if (log_lines().size() >= 300) log_lines().pop_front();
    log_lines().emplace_back(line);
}

// --- files ---

struct FileRow {
    std::string rel;     // gamedata path
    std::string mod;     // "" base gamedata, else the mod id
    fs::path path;
    std::string layer;   // what this copy does in the game
    bool current_map = false;
};

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool ends_with_lua(const std::string& s) { return s.size() > 4 && lower(s.substr(s.size() - 4)) == ".lua"; }

std::string current_map() {
#if defined(SH_LUA)
    const char* m = ShScript_MapName();
    if (m != nullptr && *m != '\0') return m;
#endif
    const char* n = gs::map_name(gs::current_map());
    return n != nullptr ? n : "";
}

fs::path first_mods_dir() {
    const auto& dirs = sh::mods::registry().config().dirs;
    return dirs.empty() ? fs::path() : dirs.front();
}

std::vector<FileRow> scan_files() {
    sh::mods::Registry& reg = sh::mods::registry();
    const fs::path base = reg.config().base_dir;
    std::map<std::string, std::string> rels;  // lower -> as written
    std::error_code ec;
    if (!base.empty() && fs::is_directory(base, ec)) {
        for (fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end;
             it != end; it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            const std::string rel = fs::relative(it->path(), base, ec).generic_string();
            if (ends_with_lua(rel)) rels.emplace(lower(rel), rel);
        }
    }
    for (const sh::mods::ResolvedMod* m : reg.active())
        for (const sh::mods::ModFile& f : reg.files(m->candidate.manifest.id))
            if (ends_with_lua(f.path)) rels.emplace(lower(f.path), f.path);

    const std::string map_prefix = "maps/" + lower(current_map()) + "/";
    std::vector<FileRow> rows;
    for (const auto& [key, rel] : rels) {
        std::vector<std::string> warnings;
        const sh::mods::LuaLayers layers = sh::mods::lua_layers(first_mods_dir(), base, rel, warnings);
        const bool here = !current_map().empty() && key.rfind(map_prefix, 0) == 0;
        std::string exts;
        for (const sh::mods::LuaLayer& l : layers.extensions) exts += (exts.empty() ? "" : ", ") + l.mod.id;
        if (fs::is_regular_file(base / rel, ec)) {
            FileRow r{rel, "", base / rel, "", here};
            r.layer = layers.base_owner != "silenthill" ? "base, replaced by " + layers.base_owner
                      : exts.empty()                    ? "base"
                                                        : "base + extensions: " + exts;
            rows.push_back(std::move(r));
        }
        for (const sh::mods::ResolvedMod* m : reg.active()) {
            const std::string& id = m->candidate.manifest.id;
            const fs::path p = m->candidate.root / rel;
            if (!fs::is_regular_file(p, ec)) continue;
            FileRow r{rel, id, p, "", here};
            if (layers.base_owner == id) r.layer = "mod " + id + " (replaces the base whole)";
            else if (std::any_of(layers.extensions.begin(), layers.extensions.end(),
                                 [&](const sh::mods::LuaLayer& l) { return l.mod.id == id; }))
                r.layer = "mod " + id + " (extension after the base)";
            else r.layer = "mod " + id + " (not run: mod.json has no \"scripts\": true)";
            rows.push_back(std::move(r));
        }
    }
    std::stable_sort(rows.begin(), rows.end(),
                     [](const FileRow& a, const FileRow& b) { return a.current_map > b.current_map; });
    return rows;
}

bool writable(const fs::path& p) {
    std::ofstream f(p, std::ios::binary | std::ios::app);
    return f.is_open();
}

Result<std::string> read_text(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return Result<std::string>::fail("cannot read " + p.generic_string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return Result<std::string>::ok(ss.str());
}

Result<void> write_text(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    const fs::path tmp = p.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return Result<void>::fail("cannot write " + tmp.generic_string());
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) return Result<void>::fail("cannot write " + tmp.generic_string());
    }
    fs::rename(tmp, p, ec);
    if (ec) return Result<void>::fail("cannot replace " + p.generic_string() + ": " + ec.message());
    return Result<void>::ok();
}

// "" when the text compiles; else Lua's message.
std::string syntax_error(const std::string& text, const std::string& chunk) {
#if defined(SH_LUA)
    lua_State* L = luaL_newstate();
    if (L == nullptr) return "cannot create a Lua state";
    const std::string name = "=" + chunk;
    std::string err;
    if (luaL_loadbufferx(L, text.data(), text.size(), name.c_str(), "t") != LUA_OK)
        err = lua_tostring(L, -1) != nullptr ? lua_tostring(L, -1) : "cannot compile";
    lua_close(L);
    return err;
#else
    (void)text;
    (void)chunk;
    return "";
#endif
}

// --- the editor ---

struct Editor {
    std::vector<FileRow> rows;
    bool rows_valid = false;
    char filter[96] = "";

    std::string rel;           // open file, "" none
    std::string source_mod;    // "" base
    fs::path source;
    bool source_writable = false;
    std::string text;
    std::string saved_text;
    std::vector<int> line_start;
    std::vector<dt::LuaLexState> line_state;  // lexer state at each line's start
    bool layout_dirty = true;

    std::vector<std::string> undo, redo;
    std::chrono::steady_clock::time_point last_edit{};

    int cursor = 0, cursor_seen = -1;
    int sel_a = 0, sel_b = 0;  // the widget's selection (equal: none)
    int pending_sel_a = -1, pending_sel_b = -1;
    bool focus_editor = false, focus_find = false, focus_goto = false;
    int scroll_to_line = -1;
    bool deactivate = false;

    char find[128] = "";
    int match_a = -1, match_b = -1;
    int goto_line = 1;

    int error_line = 0;
    std::string status;
    bool status_error = false;

    int target_mod = 0;          // index into mod_choices(); last = new mod
    char new_mod[64] = "lua_edits";
    bool replace_whole = true;

    char lua_line[256] = "";
    int lua_target = 1;           // 0 game, 1 map
    std::deque<std::string> lua_out;
    int snippet_form = 0;

    // The mod's tests (tests/*.lua): the mod they were run for and the report of each file.
    int test_mod = 0;
    std::string tests_for;
    std::vector<std::pair<std::string, dt::LuaTestReport>> test_reports;
    bool tests_running = false;

    // What the running scripts have (asked from the live Lua states) and the descriptions of it.
    dt::LuaNames names;
    bool names_valid = false, names_requested = false;
    std::string names_map;
    dt::LuaApiDocs docs;
    bool docs_loaded = false;
    std::vector<const dt::LuaName*> api_rows;  // the reference list: the functions of `names`
    int api_described = 0;
    char api_filter[96] = "";
    std::vector<const dt::LuaName*> api_view;  // api_rows through the filter
    std::string api_view_filter;
    bool api_view_dirty = true;
    int left_tab = -1;  // a tab to select once: 0 files, 1 API

    // Completion at the caret.
    bool comp_open = false;
    std::vector<dt::LuaCompletion> comp;
    dt::LuaCompletionContext comp_ctx;
    int comp_sel = 0, comp_cursor = -1;
    bool comp_scroll = false;
    struct Edit {
        int begin = 0, end = 0;  // replaces [begin, end)
        std::string text;
        int caret = 0;
    };
    std::optional<Edit> pending_edit;  // applied inside the text widget while it is active
    dt::LuaCallContext call;
    int call_cursor = -2;

    // "New script".
    bool new_request = false;
    int new_kind = 0, new_mod_index = 0;
    char new_subject[64] = "";
    char new_mod_id[64] = "my_mod";
    std::string new_error;

    // Sizes kept between frames: the measured height under the code and the widest line in pixels.
    float bottom_h = 0;
    float widest_px = 0, widest_font = 0;
    bool width_dirty = true;
    // The code's scroll kept over the frames after a focus request (see draw_code).
    int hold_scroll = 0;
    float hold_x = 0, hold_y = 0;
    // The scroll of each region and the panel window's scroll range, for `luaedit status`.
    float scroll_files = 0, scroll_api = 0, scroll_code = 0, scroll_window_max = 0;
};

Editor& ed() {
    static Editor e;
    return e;
}

void set_status(const std::string& s, bool error) {
    Editor& e = ed();
    e.status = s;
    e.status_error = error;
    SH_LOGF(GAME, error ? SH_LOGLV_WARN : SH_LOGLV_INFO, "[LUAEDIT] %s", s.c_str());
}

void relayout() {
    Editor& e = ed();
    e.line_start.assign(1, 0);
    for (int i = 0; i < static_cast<int>(e.text.size()); ++i)
        if (e.text[static_cast<std::size_t>(i)] == '\n') e.line_start.push_back(i + 1);
    e.line_state.assign(e.line_start.size(), {});
    dt::LuaLexState st;
    for (std::size_t l = 0; l < e.line_start.size(); ++l) {
        e.line_state[l] = st;
        const int b = e.line_start[l];
        const int end = l + 1 < e.line_start.size() ? e.line_start[l + 1] - 1 : static_cast<int>(e.text.size());
        (void)dt::tokenize_lua_line(std::string_view(e.text).substr(static_cast<std::size_t>(b),
                                                                    static_cast<std::size_t>(end - b)),
                                    st);
    }
    e.layout_dirty = false;
    e.width_dirty = true;
}

int line_of(int offset) {
    const Editor& e = ed();
    const auto it = std::upper_bound(e.line_start.begin(), e.line_start.end(), offset);
    return static_cast<int>(it - e.line_start.begin());  // 1-based
}

// A change made from outside the widget (undo, insert, open): the widget lets go so it reads the text again.
void replace_text(std::string text, bool keep_undo) {
    Editor& e = ed();
    if (keep_undo && text != e.text) {
        e.undo.push_back(e.text);
        if (e.undo.size() > 200) e.undo.erase(e.undo.begin());
        e.redo.clear();
    }
    e.text = std::move(text);
    e.layout_dirty = true;
    e.deactivate = true;
    e.match_a = e.match_b = -1;
    e.call_cursor = -2;
    e.comp_open = false;
}

// --- what the running scripts have, and its descriptions ---

// gamedata/tools/lua_api.json of the base data, then the same path of every active mod (a mod describes the API it
// adds): the later file wins per name.
void load_docs() {
    Editor& e = ed();
    e.docs_loaded = true;
    e.docs.clear();
    sh::mods::Registry& reg = sh::mods::registry();
    std::vector<fs::path> files{reg.config().base_dir / "tools" / "lua_api.json"};
    for (const sh::mods::ResolvedMod* m : reg.active()) files.push_back(m->candidate.root / "tools" / "lua_api.json");
    for (const fs::path& p : files) {
        std::error_code ec;
        if (!fs::is_regular_file(p, ec)) continue;
        auto text = read_text(p);
        auto parsed = text ? e.docs.parse(text.value()) : Result<void>::fail(text.error());
        if (!parsed) SH_LOGF(GAME, SH_LOGLV_WARN, "[LUAEDIT] %s: %s", p.generic_string().c_str(), parsed.error().c_str());
    }
}

struct DocText {
    bool found = false;  // the name has an entry; its description may still be empty
    std::string signature, description;
    const char* text() const { return description.empty() ? "not described yet in tools/lua_api.json" : description.c_str(); }
};

// The description of a dotted name: its own entry, else the entry of the other name the engine gives the same
// function (sh.flag.get and sh.flag_get, api_registry.h).
DocText doc_for(const std::string& name) {
    const Editor& e = ed();
    const dt::LuaApiDoc* own = e.docs.find(name);
    if (own != nullptr && !own->description.empty()) return {true, own->signature, own->description};
#if defined(SH_LUA)
    for (const sh::script::ApiAlias& al : sh::script::api_aliases()) {
        const std::string canonical = std::string("sh.") + al.group + "." + al.name;
        const std::string legacy = std::string("sh.") + al.legacy;
        const std::string* other = name == canonical ? &legacy : name == legacy ? &canonical : nullptr;
        if (other == nullptr) continue;
        const dt::LuaApiDoc* d = e.docs.find(*other);
        if (d != nullptr && !d->description.empty())
            return {true, dt::signature_as(d->signature, *other, name), d->description};
    }
#endif
    // An entry with a signature only.
    if (own != nullptr) return {true, own->signature, std::string()};
    return {};
}

// The argument list of a signature: "(item[, n]) -> count" of "sh.give_item(item[, n]) -> count".
std::string signature_tail(const std::string& signature) {
    const std::size_t open = signature.find('(');
    return open == std::string::npos ? std::string() : signature.substr(open);
}

void rebuild_api_rows() {
    Editor& e = ed();
    e.api_rows.clear();
    e.api_view.clear();
    e.api_view_dirty = true;
    e.api_described = 0;
    for (const auto& [name, n] : e.names.all()) {
        if (n.kind != dt::LuaNameKind::Function) continue;
        const bool described = !doc_for(name).description.empty();
        // The scripts' own globals are listed only when described (the host's helpers: wait, say, extend).
        if (n.origin == dt::LuaOrigin::Script && !described) continue;
        e.api_rows.push_back(&n);
        if (described) ++e.api_described;
    }
    std::stable_sort(e.api_rows.begin(), e.api_rows.end(), [](const dt::LuaName* a, const dt::LuaName* b) {
        return static_cast<int>(a->origin) < static_cast<int>(b->origin);
    });
}

// Asks the live Lua states (the game script's and the map script's) for their globals. Runs outside the frame's
// drawing (deferred), like every other call into the scripts.
void load_names() {
    Editor& e = ed();
    e.names_requested = false;
    e.names.clear();
    if (!e.docs_loaded) load_docs();
#if defined(SH_LUA)
    const std::string snippet(dt::lua_names_snippet());
    // What a plain Lua state has is the Lua library; the rest of the sandbox is the engine's or the scripts'.
    dt::LuaNames library;
    if (lua_State* L = luaL_newstate()) {
        luaL_openlibs(L);
        const std::string chunk = "return " + snippet;
        if (luaL_dostring(L, chunk.c_str()) == LUA_OK && lua_isstring(L, -1))
            dt::read_lua_names(lua_tostring(L, -1), nullptr, library);
        lua_close(L);
    }
    static std::vector<char> out(1 << 20);
    int states = 0;
    for (int target = 0; target < 2; ++target) {
        if (ShScript_RunSnippet(target, snippet.c_str(), out.data(), static_cast<int>(out.size())) == 0) continue;
        dt::read_lua_names(out.data(), &library, e.names);
        ++states;
    }
    SH_LOGF(GAME, SH_LOGLV_INFO, "[LUAEDIT] %d names from %d script state(s), %d described in tools/lua_api.json",
            static_cast<int>(e.names.size()), states, static_cast<int>(e.docs.size()));
#endif
    e.names_valid = true;
    e.names_map = current_map();
    rebuild_api_rows();
}

// --- completion ---

void close_completion() {
    Editor& e = ed();
    e.comp_open = false;
    e.comp.clear();
}

// Lists the completions at the caret. `force` (Ctrl+Space) lists even with nothing typed; `moved` is a caret move
// without an edit: the list follows it only inside the word it was opened for.
void update_completion(bool force, bool moved = false) {
    Editor& e = ed();
    dt::LuaCompletionContext ctx = dt::completion_context(e.text, e.cursor);
    if (!ctx.valid) {
        if (!force) return close_completion();
        ctx.valid = true;
        ctx.begin = e.cursor;
    }
    const bool same = e.comp_open && ctx.begin == e.comp_ctx.begin && ctx.table == e.comp_ctx.table;
    if (moved && !same) return close_completion();
    e.comp = dt::lua_completions(ctx, e.names, e.text, e.cursor);
    if (e.comp.size() == 1 && e.comp[0].text == ctx.partial) e.comp.clear();  // the word is already complete
    e.comp_ctx = ctx;
    e.comp_cursor = e.cursor;
    e.comp_open = !e.comp.empty();
    if (!same) e.comp_sel = 0;
    e.comp_sel = std::clamp(e.comp_sel, 0, std::max(0, static_cast<int>(e.comp.size()) - 1));
    e.comp_scroll = true;
}

// Replaces [begin, end) with `text` and puts the caret at `caret`. Inside the active text widget the edit goes
// through its callback (its cursor and its own undo stay); otherwise the text is swapped and the widget takes the
// focus again.
void apply_edit(int begin, int end, const std::string& text, int caret, bool in_widget) {
    Editor& e = ed();
    begin = std::clamp(begin, 0, static_cast<int>(e.text.size()));
    end = std::clamp(end, begin, static_cast<int>(e.text.size()));
    if (in_widget) {
        e.pending_edit = Editor::Edit{begin, end, text, caret};
        return;
    }
    std::string t = e.text;
    t.replace(static_cast<std::size_t>(begin), static_cast<std::size_t>(end - begin), text);
    replace_text(std::move(t), true);
    e.cursor = caret;
    e.pending_sel_a = e.pending_sel_b = caret;
    e.focus_editor = true;
}

void accept_completion(int index, bool in_widget) {
    Editor& e = ed();
    if (!e.comp_open || index < 0 || index >= static_cast<int>(e.comp.size())) return;
    const std::string word = e.comp[static_cast<std::size_t>(index)].text;
    const int begin = e.comp_ctx.begin;
    close_completion();
    apply_edit(begin, std::max(begin, e.cursor), word, begin + static_cast<int>(word.size()), in_widget);
}

// The API reference's click: the call at the caret, the caret between the parentheses.
void insert_call(const std::string& name) {
    Editor& e = ed();
    if (e.rel.empty()) return set_status("open or create a file first", true);
    const int at = std::clamp(e.cursor, 0, static_cast<int>(e.text.size()));
    apply_edit(at, at, name + "()", at + static_cast<int>(name.size()) + 1, false);
}

// `root` is the mod's folder when the mod is not in the running set yet (a mod made in the editor's Mod panel).
Result<void> open_file(const std::string& rel, const std::string& mod, const fs::path& root = {}) {
    Editor& e = ed();
    sh::mods::Registry& reg = sh::mods::registry();
    fs::path p;
    if (mod.empty()) {
        p = reg.config().base_dir / rel;
    } else if (!root.empty()) {
        p = root / rel;
    } else {
        const sh::mods::ResolvedMod* m = reg.plan().find(mod);
        if (m == nullptr) return Result<void>::fail("no mod " + mod);
        p = m->candidate.root / rel;
    }
    auto text = read_text(p);
    if (!text) return Result<void>::fail(text.error());
    e.rel = rel;
    e.source_mod = mod;
    e.source = p;
    e.source_writable = !mod.empty() && writable(p);
    replace_text(text.value(), false);
    e.saved_text = e.text;
    e.undo.clear();
    e.redo.clear();
    e.error_line = 0;
    e.match_a = e.match_b = -1;
    e.scroll_to_line = 1;
    set_status("opened " + rel + (mod.empty() ? " (base gamedata, read-only)" : " (mod " + mod + ")"), false);
    return Result<void>::ok();
}

// Opens the copy the game runs first: a replacing mod's, else the base, else the first mod having it.
Result<void> open_by_path(const std::string& rel) {
    Editor& e = ed();
    if (!e.rows_valid) {
        e.rows = scan_files();
        e.rows_valid = true;
    }
    const FileRow* pick = nullptr;
    for (const FileRow& r : e.rows) {
        if (lower(r.rel) != lower(rel)) continue;
        if (r.layer.find("replaces the base") != std::string::npos) return open_file(r.rel, r.mod);
        if (pick == nullptr || (r.mod.empty() && !pick->mod.empty())) pick = &r;
    }
    if (pick == nullptr) return Result<void>::fail("no Lua file " + rel + " in the base data or an active mod");
    return open_file(pick->rel, pick->mod);
}

bool check_syntax() {
    Editor& e = ed();
    const std::string err = syntax_error(e.text, e.rel.empty() ? "untitled.lua" : e.rel);
    e.error_line = err.empty() ? 0 : dt::lua_error_line(err);
    if (!err.empty()) {
        set_status("syntax error: " + err, true);
        if (e.error_line > 0) e.scroll_to_line = e.error_line;
        return false;
    }
    set_status("syntax OK", false);
    return true;
}

// The mods the editor can save into: the registry's (running set first), then a new one.
std::vector<std::string> mod_choices() {
    std::vector<std::string> ids;
    for (const sh::mods::ResolvedMod& m : sh::mods::registry().plan().mods) ids.push_back(m.candidate.manifest.id);
    return ids;
}

// A new mod's folder also becomes enabled in load_order.json; the registry sees it at the next start.
Result<void> enable_new_mod(const std::string& id) {
    sh::mods::Registry& reg = sh::mods::registry();
    fs::path state = reg.config().state_file;
    if (state.empty()) state = first_mods_dir() / std::string(sh::mods::kUserStateFile);
    sh::mods::UserState us;
    std::error_code ec;
    if (fs::is_regular_file(state, ec)) {
        auto text = read_text(state);
        if (!text) return Result<void>::fail(text.error());
        auto parsed = sh::mods::parse_user_state(text.value());
        if (!parsed) return Result<void>::fail(state.generic_string() + ": " + parsed.error());
        us = parsed.value();
    }
    for (const sh::mods::UserEntry& u : us.mods)
        if (u.id == id) return Result<void>::ok();
    us.mods.push_back({id, true});
    return write_text(state, sh::mods::write_user_state(us));
}

// The folder of mod `id` to write into: the registry's, else a new mod in the mods folder (mod.json written, the
// mod enabled in load_order.json; `created` says so).
Result<fs::path> mod_root_for_writing(const std::string& id, bool* created) {
    *created = false;
    if (!sh::mods::valid_mod_id(id)) return Result<fs::path>::fail("not a valid mod id: " + id);
    sh::mods::Registry& reg = sh::mods::registry();
    if (const sh::mods::ResolvedMod* m = reg.plan().find(id)) return Result<fs::path>::ok(m->candidate.root);
    if (first_mods_dir().empty()) return Result<fs::path>::fail("no mods folder is configured");
    const fs::path root = first_mods_dir() / id;
    std::error_code ec;
    if (!fs::is_regular_file(root / "mod.json", ec)) {
        auto w = write_text(root / "mod.json", dt::new_mod_manifest(id));
        if (!w) return Result<fs::path>::fail(w.error());
        *created = true;
    }
    auto en = enable_new_mod(id);
    if (!en) return Result<fs::path>::fail(en.error());
    return Result<fs::path>::ok(root);
}

// What has to happen before a file written into mod `id` runs: "" when the mod is loaded and its mod.json did not
// change (a reload is enough).
std::string mod_load_note(const std::string& id, bool created, bool manifest_changed) {
    const sh::mods::ResolvedMod* m = sh::mods::registry().plan().find(id);
    if (created) return "; new mod created and enabled: restart the game to load it";
    if (m == nullptr) return "; mod " + id + " is new: restart the game to load it";
    if (m->state != sh::mods::ModState::Active) return "; mod " + id + " is not active: enable it (Options > Mods) and restart";
    if (manifest_changed) return "; mod.json changed (scripts/replace): restart to apply it";
    return "";
}

// True when `p` is inside the install's base gamedata, which the editor never writes.
bool in_base_data(const fs::path& p) {
    std::error_code ec;
    const fs::path base = fs::weakly_canonical(sh::mods::registry().config().base_dir, ec);
    const fs::path full = fs::weakly_canonical(p, ec);
    if (base.empty()) return false;
    const auto mismatch = std::mismatch(base.begin(), base.end(), full.begin(), full.end());
    return mismatch.first == base.end();
}

// The folders of gamedata/<dir> in the base data and the active mods ("maps", "chara"), sorted.
std::vector<std::string> data_folders(const char* dir) {
    sh::mods::Registry& reg = sh::mods::registry();
    std::vector<fs::path> roots{reg.config().base_dir};
    for (const sh::mods::ResolvedMod* m : reg.active()) roots.push_back(m->candidate.root);
    std::vector<std::string> out;
    for (const fs::path& root : roots) {
        std::error_code ec;
        for (fs::directory_iterator it(root / dir, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_directory(ec)) out.push_back(it->path().filename().string());
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// "New script": writes the starting text of a script of `kind` into mod `id` (a new mod when the registry has
// none of that id), makes the mod's mod.json say "scripts": true and opens the file. An existing file is opened
// as it is, never written over.
Result<void> create_script(const std::string& id, const std::string& kind, const std::string& subject) {
    Editor& e = ed();
    if (!sh::mods::valid_mod_id(id)) return Result<void>::fail("not a valid mod id: " + id);
    auto plan = dt::new_script(kind, subject, id);
    if (!plan) return Result<void>::fail(plan.error());
    const std::string rel = plan.value().relative;
    for (const dt::NewScriptKind& k : dt::new_script_kinds()) {
        if (k.id != kind || (k.subject != "map" && k.subject != "chara")) continue;
        const std::vector<std::string> known = data_folders(k.subject == "map" ? "maps" : "chara");
        if (std::find(known.begin(), known.end(), subject) == known.end())
            return Result<void>::fail("no " + std::string(k.subject) + " '" + subject + "' in the game data");
    }
    bool created = false;
    auto root = mod_root_for_writing(id, &created);
    if (!root) return Result<void>::fail(root.error());
    auto target = dt::mod_file_for(root.value(), rel);
    if (!target) return Result<void>::fail(target.error());
    if (in_base_data(target.value())) return Result<void>::fail("the base gamedata is never written: " + rel);
    std::error_code ec;
    const bool exists = fs::exists(target.value(), ec);
    std::string text = plan.value().text;
    bool manifest_changed = false;
    if (exists) {
        auto old = read_text(target.value());
        if (!old) return Result<void>::fail(old.error());
        text = old.value();
    } else {
        auto manifest = read_text(root.value() / "mod.json");
        if (!manifest) return Result<void>::fail(manifest.error());
        auto updated = dt::manifest_for_saved_lua(manifest.value(), rel, false, &manifest_changed);
        if (!updated) return Result<void>::fail(id + "/mod.json: " + updated.error());
        if (manifest_changed) {
            auto w = write_text(root.value() / "mod.json", updated.value());
            if (!w) return w;
        }
        auto w = write_text(target.value(), text);
        if (!w) return w;
    }
    e.rel = rel;
    e.source_mod = id;
    e.source = target.value();
    e.source_writable = writable(target.value());
    replace_text(std::move(text), false);
    e.saved_text = e.text;
    e.undo.clear();
    e.redo.clear();
    e.error_line = 0;
    e.scroll_to_line = 1;
    e.cursor = 0;
    e.rows_valid = false;
    std::string note = (exists ? "exists already, opened as it is: " : "created ") + id + "/" + rel;
    const std::string load = mod_load_note(id, created, manifest_changed);
    note += !load.empty() ? load : exists ? "" : "; Reload to run it";
    set_status(note, false);
    return Result<void>::ok();
}

Result<void> save_to_mod(const std::string& id_in, bool replace_whole) {
    Editor& e = ed();
    if (e.rel.empty()) return Result<void>::fail("no file is open");
    if (!check_syntax()) return Result<void>::fail(e.status);
    sh::mods::Registry& reg = sh::mods::registry();
    const std::string id = id_in;
    bool created = false;
    auto root_found = mod_root_for_writing(id, &created);
    if (!root_found) return Result<void>::fail(root_found.error());
    const fs::path root = root_found.value();
    auto target = dt::mod_file_for(root, e.rel);
    if (!target) return Result<void>::fail(target.error());
    // A whole copy of a base file replaces it; a file only mods have needs no "replace".
    std::error_code ec;
    const bool in_base = fs::is_regular_file(reg.config().base_dir / e.rel, ec);
    auto manifest = read_text(root / "mod.json");
    if (!manifest) return Result<void>::fail(manifest.error());
    bool manifest_changed = false;
    auto updated = dt::manifest_for_saved_lua(manifest.value(), e.rel, replace_whole && in_base, &manifest_changed);
    if (!updated) return Result<void>::fail(id + "/mod.json: " + updated.error());
    if (manifest_changed) {
        auto w = write_text(root / "mod.json", updated.value());
        if (!w) return w;
    }
    auto w = write_text(target.value(), e.text);
    if (!w) return w;
    e.source = target.value();
    e.source_mod = id;
    e.source_writable = true;
    e.saved_text = e.text;
    e.rows_valid = false;
    const std::string load = mod_load_note(id, created, manifest_changed);
    const std::string note = "saved " + id + "/" + e.rel + (load.empty() ? "; Reload to apply" : load);
    set_status(note, false);
    return Result<void>::ok();
}

Result<void> save_over() {
    Editor& e = ed();
    if (e.rel.empty()) return Result<void>::fail("no file is open");
    if (e.source_mod.empty() || !e.source_writable)
        return Result<void>::fail("this copy is the base gamedata (or not writable): use Save to mod");
    if (!check_syntax()) return Result<void>::fail(e.status);
    auto w = write_text(e.source, e.text);
    if (!w) return w;
    e.saved_text = e.text;
    set_status("saved over " + e.source_mod + "/" + e.rel + "; Reload to apply", false);
    return Result<void>::ok();
}

void reload_map_script() {
#if defined(SH_LUA)
    defer([] {
        // A file added to a mod since the last index is found only after a reindex.
        sh::mods::registry().reindex();
        ShScript_ReloadMapScript();
    });
    set_status("map script runs again (maps/" + current_map() + "/events.lua)", false);
#else
    set_status("no Lua in this build", true);
#endif
}

void reload_mods() {
    run_command("mods reload");
    set_status("mods reload requested (a changed map file loads the map again in place)", false);
}

void run_lua(int target, const std::string& code, const std::string& label = "") {
#if defined(SH_LUA)
    defer([target, code, label] {
        static char out[4096];
        const int ok = ShScript_RunSnippet(target, code.c_str(), out, sizeof(out));
        Editor& e = ed();
        e.lua_out.push_back(std::string(target == 1 ? "map> " : "game> ") + (label.empty() ? code : label));
        std::istringstream lines(out);
        for (std::string l; std::getline(lines, l);) e.lua_out.push_back((ok ? "  " : "  error: ") + l);
        while (e.lua_out.size() > 200) e.lua_out.pop_front();
        SH_LOGF(GAME, SH_LOGLV_INFO, "[LUAEDIT] lua %s: %s -> %s", target == 1 ? "map" : "game",
                (label.empty() ? code : label).c_str(), out);
    });
#else
    (void)target;
    (void)code;
    (void)label;
    ed().lua_out.push_back("no Lua in this build");
#endif
}

// Runs the open file, or its selection when there is one, as one chunk in a live state. The output panel and the
// log show what ran by name, not the whole text.
void run_script(int target) {
    Editor& e = ed();
    if (e.text.empty()) return;
    const bool part = e.sel_b > e.sel_a && e.sel_b <= static_cast<int>(e.text.size());
    const std::string code =
        part ? e.text.substr(static_cast<std::size_t>(e.sel_a), static_cast<std::size_t>(e.sel_b - e.sel_a)) : e.text;
    const std::string what = part ? "selection of " + e.rel : (e.rel.empty() ? std::string("the text") : e.rel);
    const int lines = static_cast<int>(std::count(code.begin(), code.end(), '\n')) + 1;
    run_lua(target, code, "-- run " + what + " (" + std::to_string(lines) + " lines)");
}

// --- tests (docs/modding/scripting.md, "Tests") ---

struct TestMod {
    std::string id;
    fs::path dir;  // <mod>/tests
};

// The mods of the load plan that have a tests folder.
std::vector<TestMod> test_mods() {
    std::vector<TestMod> out;
    for (const sh::mods::ResolvedMod& m : sh::mods::registry().plan().mods) {
        std::error_code ec;
        const fs::path dir = m.candidate.root / "tests";
        if (fs::is_directory(dir, ec)) out.push_back({m.candidate.manifest.id, dir});
    }
    return out;
}

std::vector<fs::path> test_files(const fs::path& dir) {
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& f : fs::directory_iterator(dir, ec))
        if (f.is_regular_file() && ends_with_lua(f.path().filename().string())) files.push_back(f.path());
    std::sort(files.begin(), files.end());
    return files;
}

// Runs every tests/*.lua of mod `id` in the game script's state, one file after the other, in the next update.
Result<void> run_tests(const std::string& id) {
    std::optional<TestMod> mod;
    for (const TestMod& m : test_mods())
        if (m.id == id) mod = m;
    if (!mod) return Result<void>::fail("mod " + id + " has no tests folder (tests/*.lua)");
    std::vector<std::pair<std::string, std::string>> files;  // chunk name, text
    for (const fs::path& f : test_files(mod->dir)) {
        auto text = read_text(f);
        if (!text) return Result<void>::fail(text.error());
        files.emplace_back(id + "/tests/" + f.filename().string(), std::move(text).value());
    }
    if (files.empty()) return Result<void>::fail("mod " + id + ": tests/ has no .lua file");
    Editor& e = ed();
    e.tests_for = id;
    e.test_reports.clear();
    e.tests_running = true;
#if defined(SH_LUA)
    defer([files] {
        Editor& e = ed();
        static char out[16384];
        const std::string harness(dt::lua_test_harness());
        int passed = 0, failed = 0, broken = 0;
        for (const auto& [name, text] : files) {
            ShScript_RunTestFile(0, harness.c_str(), text.c_str(), name.c_str(), out, sizeof(out));
            dt::LuaTestReport r = dt::parse_lua_test_output(out);
            passed += r.passed();
            failed += r.failed();
            if (!r.error.empty()) {
                ++broken;
                SH_LOGF(GAME, SH_LOGLV_WARN, "[LUATEST] %s: %s", name.c_str(), r.error.c_str());
            }
            for (const dt::LuaTestResult& t : r.tests)
                SH_LOGF(GAME, t.passed ? SH_LOGLV_INFO : SH_LOGLV_WARN, "[LUATEST] %s %s %s%s%s",
                        t.passed ? "PASS" : "FAIL", name.c_str(), t.name.c_str(), t.passed ? "" : ": ",
                        t.message.c_str());
            e.test_reports.emplace_back(name, std::move(r));
        }
        e.tests_running = false;
        SH_LOGF(GAME, SH_LOGLV_INFO, "[LUATEST] %s: %d passed, %d failed, %d file(s) not run", e.tests_for.c_str(),
                passed, failed, broken);
        set_status("tests of " + e.tests_for + ": " + std::to_string(passed) + " passed, " + std::to_string(failed) +
                       " failed" + (broken > 0 ? ", " + std::to_string(broken) + " file(s) did not run" : ""),
                   failed > 0 || broken > 0);
    });
#else
    e.tests_running = false;
    set_status("no Lua in this build", true);
#endif
    return Result<void>::ok();
}

std::string pick_snippet(int form) {
    const Pick& pick = last_pick();
    if (!pick.hit) return "";
    const dt::Q12Point q = dt::q12_from_geometry(pick.point.x * 256.0, pick.point.y * 256.0, pick.point.z * 256.0);
    const auto all = dt::position_snippets(q, gs::chara_rotation(0).y);
    if (all.empty()) return "";
    return all[static_cast<std::size_t>(std::clamp(form, 0, static_cast<int>(all.size()) - 1))].text;
}

void insert_at_cursor(const std::string& s) {
    Editor& e = ed();
    if (s.empty()) {
        set_status("no picked point yet (left-click the world)", true);
        return;
    }
    std::string t = e.text;
    const int at = std::clamp(e.cursor, 0, static_cast<int>(t.size()));
    t.insert(static_cast<std::size_t>(at), s);
    replace_text(std::move(t), true);
    e.cursor = at + static_cast<int>(s.size());
    e.pending_sel_a = e.pending_sel_b = e.cursor;
    e.focus_editor = true;
}

void do_undo(bool redo) {
    Editor& e = ed();
    auto& from = redo ? e.redo : e.undo;
    auto& to = redo ? e.undo : e.redo;
    if (from.empty()) return;
    to.push_back(e.text);
    e.text = std::move(from.back());
    from.pop_back();
    e.layout_dirty = true;
    e.deactivate = true;
}

bool find_next(bool from_start) {
    Editor& e = ed();
    const std::string needle = lower(e.find);
    if (needle.empty()) return false;
    const std::string hay = lower(e.text);
    const std::size_t start = from_start ? 0 : static_cast<std::size_t>(std::max(e.match_b, e.cursor));
    std::size_t at = hay.find(needle, std::min(start, hay.size()));
    if (at == std::string::npos) at = hay.find(needle);
    if (at == std::string::npos) {
        set_status(std::string("not found: ") + e.find, true);
        e.match_a = e.match_b = -1;
        return false;
    }
    e.match_a = static_cast<int>(at);
    e.match_b = static_cast<int>(at + needle.size());
    e.cursor = e.match_b;
    e.pending_sel_a = e.match_a;
    e.pending_sel_b = e.match_b;
    if (e.layout_dirty) relayout();
    e.scroll_to_line = line_of(e.match_a);
    set_status("found at line " + std::to_string(e.scroll_to_line), false);
    return true;
}

void go_to_line(int line) {
    Editor& e = ed();
    if (e.layout_dirty) relayout();
    line = std::clamp(line, 1, static_cast<int>(e.line_start.size()));
    e.cursor = e.line_start[static_cast<std::size_t>(line - 1)];
    e.pending_sel_a = e.pending_sel_b = e.cursor;
    e.scroll_to_line = line;
    e.focus_editor = true;
}

int text_callback(ImGuiInputTextCallbackData* d) {
    Editor& e = ed();
    if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        e.text.resize(static_cast<std::size_t>(d->BufTextLen));
        d->Buf = e.text.data();
        return 0;
    }
    if (d->EventFlag == ImGuiInputTextFlags_CallbackAlways) {
        if (e.pending_edit) {
            const Editor::Edit edit = *e.pending_edit;
            e.pending_edit.reset();
            const int begin = std::clamp(edit.begin, 0, d->BufTextLen);
            const int end = std::clamp(edit.end, begin, d->BufTextLen);
            d->DeleteChars(begin, end - begin);
            d->InsertChars(begin, edit.text.c_str());
            d->CursorPos = d->SelectionStart = d->SelectionEnd = std::clamp(edit.caret, 0, d->BufTextLen);
        }
        if (e.pending_sel_a >= 0) {
            d->SelectionStart = std::clamp(e.pending_sel_a, 0, d->BufTextLen);
            d->SelectionEnd = d->CursorPos = std::clamp(e.pending_sel_b, 0, d->BufTextLen);
            e.pending_sel_a = e.pending_sel_b = -1;
        }
        e.cursor = d->CursorPos;
        e.sel_a = std::min(d->SelectionStart, d->SelectionEnd);
        e.sel_b = std::max(d->SelectionStart, d->SelectionEnd);
    }
    return 0;
}

ImU32 colour(dt::LuaToken k) {
    switch (k) {
    case dt::LuaToken::Keyword: return IM_COL32(220, 140, 255, 255);
    case dt::LuaToken::String: return IM_COL32(150, 220, 120, 255);
    case dt::LuaToken::Comment: return IM_COL32(120, 135, 150, 255);
    case dt::LuaToken::Number: return IM_COL32(250, 190, 100, 255);
    case dt::LuaToken::Api: return IM_COL32(80, 210, 230, 255);
    case dt::LuaToken::ApiUnknown: return IM_COL32(255, 110, 100, 255);
    case dt::LuaToken::Library: return IM_COL32(125, 165, 255, 255);
    case dt::LuaToken::FunctionDef: return IM_COL32(255, 225, 90, 255);
    case dt::LuaToken::Call: return IM_COL32(230, 215, 160, 255);
    default: return IM_COL32(225, 225, 225, 255);
    }
}

// Keys the text widget must not see during one call: while the completion list is open the arrows, Tab and Enter
// are the list's, and Escape in the widget would put back the text it had when it took the focus. The keys read
// as up for the widget and get their state back when this goes out of scope.
struct HiddenKeys {
    struct Saved {
        ImGuiKeyData* data;
        ImGuiKeyData value;
    };
    std::vector<Saved> saved;
    void hide(ImGuiKey key) {
        ImGuiKeyData* d = ImGui::GetKeyData(key);
        saved.push_back({d, *d});
        d->Down = false;
        d->DownDuration = d->DownDurationPrev = -1.0f;
    }
    ~HiddenKeys() {
        for (const Saved& s : saved) *s.data = s.value;
    }
};

const char* kind_word(const dt::LuaCompletion& c) {
    switch (c.kind) {
    case dt::LuaCompletionKind::Keyword: return "keyword";
    case dt::LuaCompletionKind::Function:
        return c.origin == dt::LuaOrigin::Engine ? "engine function" : c.origin == dt::LuaOrigin::Library ? "Lua function" : "function";
    case dt::LuaCompletionKind::Table:
        return c.origin == dt::LuaOrigin::Engine ? "engine table" : c.origin == dt::LuaOrigin::Library ? "Lua table" : "table";
    case dt::LuaCompletionKind::Value: return "value";
    default: return "in this file";
    }
}

// The completion list under the caret (above it when there is no room below). A click accepts a row; a click
// anywhere else closes the list.
void draw_completion(const ImVec2& caret, float line_h) {
    Editor& e = ed();
    const ImGuiIO& io = ImGui::GetIO();
    const float max_h = line_h * 9.0f + ImGui::GetStyle().WindowPadding.y * 2 + ImGui::GetStyle().ItemSpacing.y * 8;
    const bool below = caret.y + line_h + max_h <= io.DisplaySize.y;
    ImGui::SetNextWindowPos(ImVec2(caret.x, below ? caret.y + line_h : caret.y), ImGuiCond_Always, ImVec2(0.0f, below ? 0.0f : 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(line_h * 14, 0), ImVec2(line_h * 44, max_h));
    ImGui::SetNextWindowBgAlpha(1.0f);  // the code under the list must not show through its rows
    const ImGuiWindowFlags flags = ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav;
    int clicked = -1;
    bool hovered = false;
    if (ImGui::Begin("##lua_completion", nullptr, flags)) {
        for (int i = 0; i < static_cast<int>(e.comp.size()); ++i) {
            const dt::LuaCompletion& c = e.comp[static_cast<std::size_t>(i)];
            ImGui::PushID(i);
            if (ImGui::Selectable(c.text.c_str(), i == e.comp_sel)) clicked = i;
            if (i == e.comp_sel && e.comp_scroll) ImGui::SetScrollHereY(0.5f);
            ImGui::SameLine();
            const DocText doc = c.kind == dt::LuaCompletionKind::Function ? doc_for(c.full) : DocText{};
            ImGui::TextDisabled("%s", doc.found ? signature_tail(doc.signature).c_str() : kind_word(c));
            ImGui::PopID();
        }
        e.comp_scroll = false;
        hovered = ImGui::IsWindowHovered();
    }
    ImGui::End();
    if (clicked >= 0) accept_completion(clicked, false);
    else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !hovered) close_completion();
}

// The signature and the description of the highlighted completion, else of the call the caret is in. The box
// keeps its height whatever it shows, so the layout around it never moves.
void draw_hint() {
    Editor& e = ed();
    const float lh = ImGui::GetTextLineHeightWithSpacing();
    ImGui::BeginChild("##hint", ImVec2(0, lh * 3 + ImGui::GetStyle().WindowPadding.y * 2), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    std::string name;
    if (e.comp_open && e.comp_sel < static_cast<int>(e.comp.size())) name = e.comp[static_cast<std::size_t>(e.comp_sel)].full;
    else name = e.call.name;
    const DocText doc = name.empty() ? DocText{} : doc_for(name);
    const dt::LuaName* known = name.empty() ? nullptr : e.names.find(name);
    std::string tip;
    if (doc.found) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colour(dt::LuaToken::Api)), "%s", doc.signature.c_str());
        ImGui::TextWrapped("%s", doc.text());
        tip = doc.signature + "\n" + doc.text();
    } else if (known != nullptr) {
        const char* what = known->kind == dt::LuaNameKind::Function ? "function" : known->kind == dt::LuaNameKind::Table ? "table" : "value";
        const char* whose = known->origin == dt::LuaOrigin::Engine    ? "engine"
                            : known->origin == dt::LuaOrigin::Library ? "Lua library"
                                                                      : "defined by the running scripts";
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(colour(known->origin == dt::LuaOrigin::Engine ? dt::LuaToken::Api : dt::LuaToken::Library)),
                           "%s", name.c_str());
        ImGui::TextDisabled("%s, %s; no description in tools/lua_api.json", what, whose);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("The function at the caret is described here. Completions open while typing (Ctrl+Space lists "
                           "them at once): Up/Down choose, Tab or Enter accepts, Esc closes.");
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    if (!tip.empty() && ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34);
        ImGui::TextUnformatted(tip.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// The signature help: while the caret is in the argument list of a described function, its parameters over the
// caret's line with the one the caret is in highlighted, and its description.
void draw_signature(const ImVec2& caret, float line_h) {
    Editor& e = ed();
    if (!e.call.in_call || e.call.name.empty()) return;
    const DocText doc = doc_for(e.call.name);
    if (!doc.found) return;
    const dt::LuaSignatureParts parts = dt::signature_parts(doc.signature);
    const int active = dt::active_parameter(parts, e.call.argument);
    ImGui::SetNextWindowPos(ImVec2(caret.x, caret.y - 2.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(ImGui::GetFontSize() * 40, line_h * 8));
    ImGui::SetNextWindowBgAlpha(0.96f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##lua_signature", nullptr, flags)) {
        const std::string& sig = doc.signature;
        const ImVec4 base = ImGui::ColorConvertU32ToFloat4(colour(dt::LuaToken::Api));
        if (active < 0) {
            ImGui::TextColored(base, "%s", sig.c_str());
        } else {
            const auto [b, en] = parts.params[static_cast<std::size_t>(active)];
            const std::string head = sig.substr(0, static_cast<std::size_t>(b));
            const std::string mid = sig.substr(static_cast<std::size_t>(b), static_cast<std::size_t>(en - b));
            const std::string tail = sig.substr(static_cast<std::size_t>(en));
            ImGui::TextColored(base, "%s", head.c_str());
            ImGui::SameLine(0, 0);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", mid.c_str());
            const ImVec2 size = ImGui::CalcTextSize(mid.c_str());
            ImGui::GetWindowDrawList()->AddLine(ImVec2(at.x, at.y + size.y), ImVec2(at.x + size.x, at.y + size.y),
                                                IM_COL32(255, 215, 80, 255));
            ImGui::SameLine(0, 0);
            ImGui::TextColored(base, "%s", tail.c_str());
        }
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 38);
        ImGui::TextUnformatted(doc.text());
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
}

void draw_code(float height) {
    Editor& e = ed();
    if (e.layout_dirty) relayout();
    const float lh = ImGui::GetTextLineHeight();
    const float cw = ImGui::CalcTextSize("M").x;
    const int lines = static_cast<int>(e.line_start.size());
    const float gutter = cw * (static_cast<float>(std::to_string(lines).size()) + 2.0f);
    const ImVec2 pad = ImGui::GetStyle().FramePadding;
    auto line_view = [&](int l) {
        const int b = e.line_start[static_cast<std::size_t>(l)];
        const int end = l + 1 < lines ? e.line_start[static_cast<std::size_t>(l + 1)] - 1 : static_cast<int>(e.text.size());
        return std::string_view(e.text).substr(static_cast<std::size_t>(b), static_cast<std::size_t>(end - b));
    };

    ImGui::BeginChild("##code", ImVec2(0, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    // The text widget is as large as the whole text, so its own inner window never has anything to scroll: this
    // child is the one region the wheel moves. The width is measured, not guessed from the character count.
    if (e.width_dirty || e.widest_font != ImGui::GetFontSize()) {
        e.widest_px = 0;
        for (int l = 0; l < lines; ++l) {
            const std::string_view line = line_view(l);
            if (!line.empty()) e.widest_px = std::max(e.widest_px, ImGui::CalcTextSize(line.data(), line.data() + line.size()).x);
        }
        e.widest_font = ImGui::GetFontSize();
        e.width_dirty = false;
    }
    const ImVec2 view = ImGui::GetContentRegionAvail();
    const ImVec2 size(std::max(view.x - gutter, e.widest_px + cw * 6 + pad.x * 2), (static_cast<float>(lines) + 2) * lh + pad.y * 2);

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + gutter);
    const ImGuiID src_id = ImGui::GetID("##src");
    if (e.deactivate) {
        if (ImGui::GetActiveID() == src_id) ImGui::ClearActiveID();
        e.deactivate = false;
    }
    if (e.focus_editor) {
        // A focus request makes ImGui scroll the focused item into view, and this item is the whole text: the
        // view would jump to its top left corner. The scroll is put back over the frames the request takes.
        ImGui::SetKeyboardFocusHere();
        e.focus_editor = false;
        e.hold_scroll = 3;
        e.hold_x = ImGui::GetScrollX();
        e.hold_y = ImGui::GetScrollY();
    }
    const std::string before_edit = e.text;
    bool changed = false;
    {
        HiddenKeys hidden;
        if (ImGui::GetActiveID() == src_id) {
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space, false)) update_completion(true);
            if (e.comp_open) {
                const int n = static_cast<int>(e.comp.size());
                if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) e.comp_sel = (e.comp_sel + 1) % n, e.comp_scroll = true;
                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) e.comp_sel = (e.comp_sel + n - 1) % n, e.comp_scroll = true;
                const bool accept = ImGui::IsKeyPressed(ImGuiKey_Tab, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                                    ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
                for (ImGuiKey key : {ImGuiKey_DownArrow, ImGuiKey_UpArrow, ImGuiKey_Tab, ImGuiKey_Enter, ImGuiKey_KeypadEnter})
                    hidden.hide(key);
                if (accept) accept_completion(e.comp_sel, true);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) close_completion();
            hidden.hide(ImGuiKey_Escape);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_InputTextCursor, IM_COL32(255, 255, 255, 255));
        changed = ImGui::InputTextMultiline(
            "##src", e.text.data(), e.text.capacity() + 1, size,
            ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_CallbackAlways,
            &text_callback);
        ImGui::PopStyleColor(3);
    }
    const ImVec2 origin = ImGui::GetItemRectMin();
    const bool active = ImGui::IsItemActive();
    if (changed) {
        // Typing within a second is one undo step.
        const auto now = std::chrono::steady_clock::now();
        if (e.undo.empty() || now - e.last_edit > std::chrono::seconds(1)) {
            e.undo.push_back(before_edit);
            if (e.undo.size() > 200) e.undo.erase(e.undo.begin());
        }
        e.last_edit = now;
        e.redo.clear();
        e.layout_dirty = true;
        e.error_line = 0;
        e.match_a = e.match_b = -1;
        relayout();
        // The list opens when a name or a '.' was typed and follows the typing while it is open.
        const char typed = e.cursor > 0 && e.cursor <= static_cast<int>(e.text.size()) ? e.text[static_cast<std::size_t>(e.cursor - 1)] : '\0';
        const bool name_char = std::isalnum(static_cast<unsigned char>(typed)) || typed == '_' || typed == '.' || typed == ':';
        if (e.comp_open || (e.text.size() > before_edit.size() && name_char)) update_completion(false);
    } else if (e.comp_open && e.cursor != e.comp_cursor) {
        update_completion(false, true);
    }
    if (e.cursor != e.call_cursor || changed) {
        e.call = dt::call_at(e.text, e.cursor);
        e.call_cursor = e.cursor;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float scroll_y = ImGui::GetScrollY();
    const float win_y = ImGui::GetWindowPos().y;
    const float win_x = ImGui::GetWindowPos().x;
    const float view_h = ImGui::GetWindowHeight();
    const int first = std::max(0, static_cast<int>((scroll_y - pad.y) / lh) - 1);
    const int last = std::min(lines - 1, first + static_cast<int>(view_h / lh) + 2);
    const float text_x = origin.x + pad.x;
    const ImVec4 clip = ImGui::GetCurrentWindow()->ClipRect.ToVec4();
    const dt::LuaNames* names = e.names.empty() ? nullptr : &e.names;
    dl->PushClipRect(ImVec2(win_x + gutter, clip.y), ImVec2(clip.z, clip.w), true);
    for (int l = first; l <= last; ++l) {
        const int b = e.line_start[static_cast<std::size_t>(l)];
        const std::string_view line = line_view(l);
        const int end = b + static_cast<int>(line.size());
        const float y = origin.y + pad.y + static_cast<float>(l) * lh;
        if (l + 1 == e.error_line)
            dl->AddRectFilled(ImVec2(win_x, y), ImVec2(clip.z, y + lh), IM_COL32(160, 30, 30, 120));
        if (e.match_a >= b && e.match_a <= end) {
            const float x0 = ImGui::CalcTextSize(line.data(), line.data() + (e.match_a - b)).x;
            const float x1 = ImGui::CalcTextSize(line.data(), line.data() + std::min(e.match_b, end) - b).x;
            dl->AddRectFilled(ImVec2(text_x + x0, y), ImVec2(text_x + x1, y + lh), IM_COL32(200, 170, 40, 110));
        }
        dt::LuaLexState st = e.line_state[static_cast<std::size_t>(l)];
        int at = 0;
        auto draw = [&](int from, int to, dt::LuaToken kind) {
            if (to <= from) return;
            const float x = ImGui::CalcTextSize(line.data(), line.data() + from).x;
            dl->AddText(ImVec2(text_x + x, y), colour(kind), line.data() + from, line.data() + to);
            if (kind == dt::LuaToken::ApiUnknown) {
                const float w = ImGui::CalcTextSize(line.data() + from, line.data() + to).x;
                dl->AddLine(ImVec2(text_x + x, y + lh - 1), ImVec2(text_x + x + w, y + lh - 1), colour(kind));
            }
        };
        for (const dt::LuaSpan& s : dt::tokenize_lua_line(line, st, names)) {
            draw(at, s.begin, dt::LuaToken::Text);
            draw(s.begin, s.end, s.kind);
            at = s.end;
        }
        draw(at, static_cast<int>(line.size()), dt::LuaToken::Text);
    }
    dl->PopClipRect();
    // The gutter stays put when the text scrolls sideways.
    dl->AddRectFilled(ImVec2(win_x, clip.y), ImVec2(win_x + gutter - cw * 0.5f, clip.w), IM_COL32(30, 32, 36, 255));
    for (int l = first; l <= last; ++l) {
        const float y = origin.y + pad.y + static_cast<float>(l) * lh;
        char n[16];
        std::snprintf(n, sizeof(n), "%*d", static_cast<int>(std::to_string(lines).size()), l + 1);
        dl->AddText(ImVec2(win_x + cw * 0.5f, y), l + 1 == e.error_line ? IM_COL32(255, 90, 90, 255) : IM_COL32(110, 115, 125, 255), n);
    }

    // Keep the cursor in view while it moves; scroll requests (find, go to line, errors) first.
    if (e.hold_scroll > 0) {
        --e.hold_scroll;
        ImGui::SetScrollX(e.hold_x);
        ImGui::SetScrollY(e.hold_y);
    }
    if (e.scroll_to_line > 0) {
        e.hold_y = std::max(0.0f, (static_cast<float>(e.scroll_to_line) - 4) * lh);
        ImGui::SetScrollY(e.hold_y);
        e.scroll_to_line = -1;
    } else if (active && e.cursor != e.cursor_seen) {
        const int l = line_of(e.cursor) - 1;
        const float y = static_cast<float>(l) * lh + pad.y;
        if (y < scroll_y) ImGui::SetScrollY(y);
        else if (y + lh * 2 > scroll_y + view_h) ImGui::SetScrollY(y + lh * 2 - view_h);
        const int b = e.line_start[static_cast<std::size_t>(l)];
        const float x = ImGui::CalcTextSize(e.text.data() + b, e.text.data() + e.cursor).x + gutter + pad.x;
        const float sx = ImGui::GetScrollX();
        if (x < sx + gutter) ImGui::SetScrollX(std::max(0.0f, x - gutter - cw * 4));
        else if (x + cw * 2 > sx + view.x) ImGui::SetScrollX(x + cw * 6 - view.x);
    }
    e.cursor_seen = e.cursor;
    e.scroll_code = ImGui::GetScrollY();

    // Where the caret is on the screen, for the completion list.
    const int caret = std::clamp(e.cursor, 0, static_cast<int>(e.text.size()));
    const int caret_line = line_of(caret) - 1;
    const int caret_b = e.line_start[static_cast<std::size_t>(caret_line)];
    const ImVec2 caret_px(text_x + ImGui::CalcTextSize(e.text.data() + caret_b, e.text.data() + caret).x,
                          origin.y + pad.y + static_cast<float>(caret_line) * lh);
    const bool caret_seen = caret_px.y + lh > win_y && caret_px.y < win_y + view_h;
    ImGui::EndChild();
    if (caret_seen) draw_signature(caret_px, lh);
    if (e.comp_open && caret_seen) draw_completion(caret_px, lh);
}

// A list that takes the height left in its table cell (the cell's bottom padding stays free, so the table is
// exactly as tall as the panel).
ImVec2 rest_of_cell() { return ImVec2(0, -ImGui::GetStyle().CellPadding.y - 1.0f); }

void draw_browser() {
    Editor& e = ed();
    if (!e.rows_valid) {
        e.rows = scan_files();
        e.rows_valid = true;
    }
    if (ImGui::SmallButton("New script...")) e.new_request = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) e.rows_valid = false;
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "filter", e.filter, sizeof(e.filter));
    ImGui::BeginChild("##files", rest_of_cell(), ImGuiChildFlags_Borders);
    const std::string f = lower(e.filter);
    bool map_header = false, rest_header = false;
    int i = 0;
    for (const FileRow& r : e.rows) {
        if (!f.empty() && lower(r.rel).find(f) == std::string::npos && lower(r.mod).find(f) == std::string::npos) continue;
        if (r.current_map && !map_header) {
            ImGui::SeparatorText(("map " + current_map()).c_str());
            map_header = true;
        } else if (!r.current_map && !rest_header) {
            ImGui::SeparatorText("all");
            rest_header = true;
        }
        ImGui::PushID(i++);
        const bool open = r.rel == e.rel && r.mod == e.source_mod;
        if (ImGui::Selectable(r.rel.c_str(), open)) {
            const std::string rel = r.rel, mod = r.mod;
            auto res = open_file(rel, mod);
            if (!res) set_status(res.error(), true);
            ImGui::PopID();
            break;  // open_file may rescan; the rows are not touched after it
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", r.layer.c_str(), r.path.generic_string().c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("[%s]", r.mod.empty() ? "base" : r.mod.c_str());
        ImGui::PopID();
    }
    e.scroll_files = ImGui::GetScrollY();
    ImGui::EndChild();
}

// The rows of the API reference that match the filter (in the name, the signature or the description).
std::vector<const dt::LuaName*> filtered_api() {
    const Editor& e = ed();
    const std::string f = lower(e.api_filter);
    if (f.empty()) return e.api_rows;
    std::vector<const dt::LuaName*> out;
    for (const dt::LuaName* n : e.api_rows) {
        bool match = lower(n->name).find(f) != std::string::npos;
        if (!match) {
            const DocText doc = doc_for(n->name);
            match = doc.found && (lower(doc.signature).find(f) != std::string::npos || lower(doc.description).find(f) != std::string::npos);
        }
        if (match) out.push_back(n);
    }
    return out;
}

// The API reference: every function the running scripts have (the engine's, the host's helpers, the Lua library),
// with its description under the pointer; a click puts the call at the caret.
void draw_api() {
    Editor& e = ed();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##apifilter", "search names and descriptions", e.api_filter, sizeof(e.api_filter));
    if (e.api_view_dirty || e.api_view_filter != e.api_filter) {
        e.api_view = filtered_api();
        e.api_view_filter = e.api_filter;
        e.api_view_dirty = false;
    }
    const std::vector<const dt::LuaName*>& s_rows = e.api_view;
    ImGui::TextDisabled("%d of %d functions (%d described); click inserts", static_cast<int>(s_rows.size()),
                        static_cast<int>(e.api_rows.size()), e.api_described);
    ImGui::BeginChild("##api", rest_of_cell(), ImGuiChildFlags_Borders);
    if (e.api_rows.empty()) ImGui::TextDisabled(e.names_valid ? "no script state is running" : "reading the script states...");
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(s_rows.size()));
    std::string insert;
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const dt::LuaName* n = s_rows[static_cast<std::size_t>(i)];
            const DocText doc = doc_for(n->name);
            ImGui::PushID(i);
            ImGui::PushStyleColor(ImGuiCol_Text, colour(n->origin == dt::LuaOrigin::Engine    ? dt::LuaToken::Api
                                                        : n->origin == dt::LuaOrigin::Library ? dt::LuaToken::Library
                                                                                              : dt::LuaToken::Call));
            if (ImGui::Selectable(n->name.c_str(), false)) insert = n->name;
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34);
                ImGui::TextUnformatted(doc.found ? doc.signature.c_str() : n->name.c_str());
                if (doc.found) ImGui::TextUnformatted(doc.text());
                else ImGui::TextDisabled("no description in tools/lua_api.json");
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            if (doc.found) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", signature_tail(doc.signature).c_str());
            }
            ImGui::PopID();
        }
    }
    e.scroll_api = ImGui::GetScrollY();
    ImGui::EndChild();
    if (!insert.empty()) insert_call(insert);
}

void draw_left() {
    Editor& e = ed();
    if (ImGui::BeginTabBar("##left")) {
        if (ImGui::BeginTabItem("Files", nullptr, e.left_tab == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
            draw_browser();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("API", nullptr, e.left_tab == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
            draw_api();
            ImGui::EndTabItem();
        }
        e.left_tab = -1;
        ImGui::EndTabBar();
    }
}

// The mod the save bar and "New script" write into: an index into mod_choices(), past its end the typed new id.
std::string chosen_mod(int index, const char* typed) {
    const std::vector<std::string> mods = mod_choices();
    return index >= 0 && index < static_cast<int>(mods.size()) ? mods[static_cast<std::size_t>(index)] : std::string(typed);
}

void mod_combo(const char* id, int* index, char* typed, std::size_t typed_cap) {
    const std::vector<std::string> mods = mod_choices();
    const int n = static_cast<int>(mods.size());
    *index = std::clamp(*index, 0, n);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9);
    if (ImGui::BeginCombo(id, *index < n ? mods[static_cast<std::size_t>(*index)].c_str() : "new mod...")) {
        for (int i = 0; i <= n; ++i)
            if (ImGui::Selectable(i < n ? mods[static_cast<std::size_t>(i)].c_str() : "new mod...", i == *index)) *index = i;
        ImGui::EndCombo();
    }
    if (*index == n) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
        ImGui::PushID(id);
        ImGui::InputText("##newmod", typed, typed_cap);
        ImGui::PopID();
    }
}

void draw_new_script() {
    Editor& e = ed();
    static std::vector<std::string> s_maps, s_charas;
    if (e.new_request) {
        e.new_request = false;
        e.new_error.clear();
        s_maps = data_folders("maps");
        s_charas = data_folders("chara");
        ImGui::OpenPopup("New script");
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("New script", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    const auto kinds = dt::new_script_kinds();
    e.new_kind = std::clamp(e.new_kind, 0, static_cast<int>(kinds.size()) - 1);
    const dt::NewScriptKind& kind = kinds[static_cast<std::size_t>(e.new_kind)];
    const float wide = ImGui::GetFontSize() * 26;
    ImGui::SetNextItemWidth(wide);
    if (ImGui::BeginCombo("kind", std::string(kind.label).c_str())) {
        for (int i = 0; i < static_cast<int>(kinds.size()); ++i)
            if (ImGui::Selectable(std::string(kinds[static_cast<std::size_t>(i)].label).c_str(), i == e.new_kind)) {
                e.new_kind = i;
                e.new_subject[0] = '\0';
            }
        ImGui::EndCombo();
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wide + ImGui::GetFontSize() * 6);
    ImGui::TextDisabled("%s", std::string(kind.description).c_str());
    ImGui::PopTextWrapPos();
    if (kind.subject == "map" || kind.subject == "chara") {
        const std::vector<std::string>& known = kind.subject == "map" ? s_maps : s_charas;
        if (e.new_subject[0] == '\0' && kind.subject == "map")
            std::snprintf(e.new_subject, sizeof(e.new_subject), "%s", current_map().c_str());
        ImGui::SetNextItemWidth(wide);
        if (ImGui::BeginCombo(kind.subject == "map" ? "map" : "character", e.new_subject)) {
            for (const std::string& name : known)
                if (ImGui::Selectable(name.c_str(), name == e.new_subject))
                    std::snprintf(e.new_subject, sizeof(e.new_subject), "%s", name.c_str());
            ImGui::EndCombo();
        }
    } else if (kind.subject == "name") {
        ImGui::SetNextItemWidth(wide);
        ImGui::InputTextWithHint("name", "file name without .lua", e.new_subject, sizeof(e.new_subject));
    }
    mod_combo("mod", &e.new_mod_index, e.new_mod_id, sizeof(e.new_mod_id));
    const std::string mod = chosen_mod(e.new_mod_index, e.new_mod_id);
    const auto plan = dt::new_script(kind.id, e.new_subject, mod);
    if (plan) ImGui::Text("writes mods/%s/%s", mod.c_str(), plan.value().relative.c_str());
    else ImGui::TextDisabled("%s", plan.error().c_str());
    if (!e.new_error.empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", e.new_error.c_str());
    ImGui::BeginDisabled(!plan);
    if (ImGui::Button("Create")) {
        auto r = create_script(mod, std::string(kind.id), e.new_subject);
        if (r) ImGui::CloseCurrentPopup();
        else e.new_error = r.error();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void draw_save_bar() {
    Editor& e = ed();
    mod_combo("##mod", &e.target_mod, e.new_mod, sizeof(e.new_mod));
    ImGui::SameLine();
    if (ImGui::Button("Save to mod")) {
        auto r = save_to_mod(chosen_mod(e.target_mod, e.new_mod), e.replace_whole);
        if (!r) set_status(r.error(), true);
    }
    ImGui::SameLine();
    ImGui::Checkbox("replace whole", &e.replace_whole);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Lists the path in mod.json \"replace\": the mod's copy runs instead of the base file,\n"
                          "not as an extension after it (docs/modding/scripting.md).");
    ImGui::SameLine();
    ImGui::BeginDisabled(!e.source_writable);
    if (ImGui::Button("Save over")) {
        auto r = save_over();
        if (!r) set_status(r.error(), true);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Check")) check_syntax();
    if (ImGui::Button("Reload (mods reload)")) reload_mods();
    ImGui::SameLine();
    if (ImGui::Button("Re-run map script")) reload_map_script();
}

// The height of the Lua line's output; the Log tab is as tall as the whole Lua tab, so the tabs never change the
// layout above them.
float lua_out_height() { return ImGui::GetTextLineHeightWithSpacing() * 3.2f; }

void draw_lua_line() {
    Editor& e = ed();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.5f);
    ImGui::Combo("##target", &e.lua_target, "game\0map\0");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 4);
    bool run = ImGui::InputTextWithHint("##lua", "Lua: sh.player_pos()  (Enter runs)", e.lua_line, sizeof(e.lua_line),
                                        ImGuiInputTextFlags_EnterReturnsTrue);
    if (run) ImGui::SetKeyboardFocusHere(-1);
    ImGui::SameLine();
    run |= ImGui::Button("Run");
    if (run && e.lua_line[0] != '\0') run_lua(e.lua_target, e.lua_line);
    ImGui::BeginDisabled(e.text.empty());
    const bool part = e.sel_b > e.sel_a;
    if (ImGui::Button(part ? "Run selection" : "Run file")) run_script(e.lua_target);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("in the %s script's state (F5)", e.lua_target == 1 ? "map" : "game");
    ImGui::BeginChild("##luaout", ImVec2(0, lua_out_height()), ImGuiChildFlags_Borders);
    for (const std::string& l : e.lua_out) ImGui::TextUnformatted(l.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}

// The Tests tab: a mod's tests/*.lua run in the game script's state, each test passed or failed.
void draw_tests() {
    Editor& e = ed();
    const std::vector<TestMod> mods = test_mods();
    if (mods.empty()) {
        ImGui::TextDisabled(
            "No mod has a tests folder. A mod's tests/*.lua define test_* functions (or call "
            "test(name, fn)); see docs/modding/scripting.md, \"Tests\".");
        return;
    }
    e.test_mod = std::clamp(e.test_mod, 0, static_cast<int>(mods.size()) - 1);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
    if (ImGui::BeginCombo("##testmod", mods[static_cast<std::size_t>(e.test_mod)].id.c_str())) {
        for (int i = 0; i < static_cast<int>(mods.size()); ++i)
            if (ImGui::Selectable(mods[static_cast<std::size_t>(i)].id.c_str(), i == e.test_mod)) e.test_mod = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(e.tests_running);
    if (ImGui::Button("Run tests")) {
        auto r = run_tests(mods[static_cast<std::size_t>(e.test_mod)].id);
        if (!r) set_status(r.error(), true);
    }
    ImGui::EndDisabled();
    ImGui::BeginChild("##tests", ImVec2(0, lua_out_height()), ImGuiChildFlags_Borders);
    if (e.test_reports.empty()) ImGui::TextDisabled(e.tests_running ? "running..." : "not run yet");
    const ImVec4 ok(0.55f, 0.9f, 0.55f, 1.0f), bad(1.0f, 0.5f, 0.45f, 1.0f);
    for (const auto& [file, r] : e.test_reports) {
        ImGui::TextColored(r.failed() > 0 || !r.error.empty() ? bad : ok, "%s: %d passed, %d failed", file.c_str(),
                           r.passed(), r.failed());
        if (!r.error.empty()) ImGui::TextColored(bad, "  did not run: %s", r.error.c_str());
        for (const dt::LuaTestResult& t : r.tests) {
            if (t.passed)
                ImGui::TextColored(ok, "  PASS %s", t.name.c_str());
            else
                ImGui::TextColored(bad, "  FAIL %s: %s", t.name.c_str(), t.message.c_str());
        }
        for (const std::string& l : r.output) ImGui::TextDisabled("  | %s", l.c_str());
    }
    ImGui::EndChild();
}

void draw_log() {
    Editor& e = ed();
    std::vector<std::string> mine;
    {
        std::lock_guard lock(log_mutex());
        const std::string name = e.rel.empty() ? std::string() : lower(e.rel);
        for (const std::string& l : log_lines())
            if (!name.empty() && lower(l).find(name) != std::string::npos) mine.push_back(l);
    }
    ImGui::BeginChild("##log", ImVec2(0, ImGui::GetFrameHeightWithSpacing() + lua_out_height()), ImGuiChildFlags_Borders);
    if (mine.empty()) ImGui::TextDisabled("no Lua warnings or errors in the log for this file");
    for (const std::string& l : mine) ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "%s", l.c_str());
    ImGui::EndChild();
}

void draw_toolbar() {
    Editor& e = ed();
    if (e.rel.empty()) {
        ImGui::TextDisabled("Pick a file on the left (the current map's files are on top) or make one with New script.");
    } else {
        const bool dirty = e.text != e.saved_text;
        ImGui::Text("%s%s", e.rel.c_str(), dirty ? " *" : "");
        ImGui::SameLine();
        ImGui::TextDisabled("[%s]", e.source_mod.empty() ? "base, read-only" : e.source_mod.c_str());
    }
    // Find, go to line, undo.
    if (e.focus_find) {
        ImGui::SetKeyboardFocusHere();
        e.focus_find = false;
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
    if (ImGui::InputTextWithHint("##find", "find (Ctrl+F)", e.find, sizeof(e.find), ImGuiInputTextFlags_EnterReturnsTrue)) {
        find_next(false);
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Next")) find_next(false);
    ImGui::SameLine();
    if (e.focus_goto) {
        ImGui::SetKeyboardFocusHere();
        e.focus_goto = false;
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4);
    if (ImGui::InputInt("##goto", &e.goto_line, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) go_to_line(e.goto_line);
    ImGui::SameLine();
    if (ImGui::Button("Go to line")) go_to_line(e.goto_line);
    ImGui::SameLine();
    ImGui::BeginDisabled(e.undo.empty());
    if (ImGui::Button("Undo")) do_undo(false);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(e.redo.empty());
    if (ImGui::Button("Redo")) do_undo(true);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!last_pick().hit);
    if (ImGui::Button("Insert pick")) insert_at_cursor(pick_snippet(e.snippet_form));
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
    ImGui::Combo("##form", &e.snippet_form, "player pos\0vec\0spawn\0table\0json\0camera\0");
}

void draw_panel() {
    Editor& e = ed();
    const ImGuiIO& io = ImGui::GetIO();
    static int s_placed = 0;
    if (s_placed < 3) {
        // A text editor needs room the host's narrow column does not give; the user moves it from here. The
        // window's first frames fit it to its contents, so the size is set over those.
        ++s_placed;
        ImGui::SetWindowSize(ImVec2(io.DisplaySize.x * 0.66f, io.DisplaySize.y * 0.86f));
        ImGui::SetWindowPos(ImVec2(io.DisplaySize.x * 0.32f, io.DisplaySize.y * 0.07f));
    }
    if ((!e.names_valid || e.names_map != current_map()) && !e.names_requested) {
        e.names_requested = true;
        defer(&load_names);
    }
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (focused && ImGui::IsKeyPressed(ImGuiKey_F5, false)) run_script(e.lua_target);
    if (focused && io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) e.focus_find = true;
        if (ImGui::IsKeyPressed(ImGuiKey_G, false)) e.focus_goto = true;
        if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            auto r = e.source_writable ? save_over() : save_to_mod(chosen_mod(e.target_mod, e.new_mod), e.replace_whole);
            if (!r) set_status(r.error(), true);
        }
    }
    e.scroll_window_max = ImGui::GetScrollMaxY();

    // The whole panel is one child of exactly the window's inner size that takes no wheel itself: the window has
    // nothing to scroll and no scrollbar, and a wheel turn over a part that cannot scroll moves nothing.
    ImGui::BeginChild("##root", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (ImGui::BeginTable("##layout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("files", ImGuiTableColumnFlags_WidthStretch, 0.28f);
        ImGui::TableSetupColumn("editor", ImGuiTableColumnFlags_WidthStretch, 0.72f);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        draw_left();
        ImGui::TableSetColumnIndex(1);
        draw_toolbar();

        // The code takes what the rows under it leave. Their height is measured (last frame's): it follows the
        // font and the interface scale without a table of sizes here.
        const float line = ImGui::GetTextLineHeightWithSpacing();
        if (e.bottom_h <= 0) e.bottom_h = line * 12;
        const float rest = ImGui::GetContentRegionAvail().y - ImGui::GetStyle().CellPadding.y - 1.0f;
        draw_code(std::max(line * 3, rest - e.bottom_h));
        const float bottom_top = ImGui::GetCursorPosY();
        draw_hint();
        draw_save_bar();
        if (!e.status.empty())
            ImGui::TextColored(e.status_error ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "%s",
                               e.status.c_str());
        else
            ImGui::TextDisabled("Ctrl+S saves (over a mod's copy, else to the chosen mod); base gamedata is never written");
        if (!e.status.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", e.status.c_str());
        if (ImGui::BeginTabBar("##bottom")) {
            if (ImGui::BeginTabItem("Lua")) {
                draw_lua_line();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Tests")) {
                draw_tests();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Log")) {
                draw_log();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        e.bottom_h = ImGui::GetCursorPosY() - bottom_top;
        ImGui::EndTable();
    }
    draw_new_script();
    ImGui::EndChild();
}

// --- console: `luaedit`, the panel's actions by name for scripted runs ---

void register_luaedit_command() {
    using sh::console::ArgSpec;
    using sh::console::ArgType;
    sh::console::CommandSpec c;
    c.name = "luaedit";
    c.group = "debug";
    c.help =
        "drive the editor's Lua panel: open <path>, new <mod> <kind> [map|character|name], save_mod <id> [replace "
        "0|1], "
        "save_over, check, find <text>, goto <line>, caret <line> <column>, sub <from> => <to>, type <text>, "
        "complete [row], accept [row], undo, redo, insert_pick [form], reload [map|mods], run <game|map> <code>, "
        "run_file <game|map> [from to], test [mod], sig, api [filter], api_insert <name>, names [file], docs, wheel "
        "<x> <y> <notches>, status";
    c.args = {
        ArgSpec{"action",
                ArgType::Enum,
                false,
                "what to do",
                {"open", "new",      "save_mod", "save_over",  "check", "find",        "goto",   "caret", "sub",
                 "type", "complete", "accept",   "undo",       "redo",  "insert_pick", "reload", "run",   "run_file",
                 "test", "sig",      "api",      "api_insert", "names", "docs",        "wheel",  "status"},
                {}},
        ArgSpec{"rest", ArgType::Rest, true, "the action's arguments", {}, {}}};
    c.details = "docs/modding/tools.md. Panel id tools.lua_editor (editorpanel tools.lua_editor on).";
    c.run = [](sh::console::Context& ctx, const sh::console::Args& args) -> Result<void> {
        Editor& e = ed();
        const std::string& a = args.str(0);
        const std::string rest = args.joined(1);
        auto report = [&](const Result<void>& r) -> Result<void> {
            if (!r) return r;
            ctx.out.info("luaedit: " + e.status);
            return Result<void>::ok();
        };
        if (a == "open") return report(open_by_path(rest));
        if (a == "save_mod") {
            std::istringstream ws(rest);
            std::string id;
            int replace = 1;
            ws >> id >> replace;
            if (id.empty()) id = e.new_mod;
            return report(save_to_mod(id, replace != 0));
        }
        if (a == "save_over") return report(save_over());
        if (a == "check") {
            check_syntax();
            ctx.out.info("luaedit: " + e.status + (e.error_line > 0 ? " (line " + std::to_string(e.error_line) + ")" : ""));
            return Result<void>::ok();
        }
        if (a == "find") {
            std::snprintf(e.find, sizeof(e.find), "%s", rest.c_str());
            find_next(true);
            ctx.out.info("luaedit: " + e.status);
            return Result<void>::ok();
        }
        if (a == "goto") {
            go_to_line(std::atoi(rest.c_str()));
            return Result<void>::ok();
        }
        if (a == "sub") {
            const std::size_t sep = rest.find(" => ");
            if (sep == std::string::npos) return Result<void>::fail("usage: luaedit sub <from> => <to>");
            const std::string from = rest.substr(0, sep), to = rest.substr(sep + 4);
            const std::size_t at = e.text.find(from);
            if (at == std::string::npos) return Result<void>::fail("luaedit sub: not found: " + from);
            std::string t = e.text;
            t.replace(at, from.size(), to);
            replace_text(std::move(t), true);
            e.cursor = static_cast<int>(at + to.size());
            relayout();
            e.scroll_to_line = line_of(static_cast<int>(at));
            ctx.out.info("luaedit: replaced at line " + std::to_string(e.scroll_to_line));
            return Result<void>::ok();
        }
        if (a == "undo" || a == "redo") {
            do_undo(a == "redo");
            return Result<void>::ok();
        }
        if (a == "insert_pick") {
            insert_at_cursor(pick_snippet(rest.empty() ? e.snippet_form : std::atoi(rest.c_str())));
            return Result<void>::ok();
        }
        if (a == "reload") {
            rest == "mods" ? reload_mods() : reload_map_script();
            ctx.out.info("luaedit: " + e.status);
            return Result<void>::ok();
        }
        if (a == "run") {
            const std::size_t sp = rest.find(' ');
            const std::string target = rest.substr(0, sp);
            if (sp == std::string::npos || (target != "game" && target != "map"))
                return Result<void>::fail("usage: luaedit run <game|map> <code>");
            run_lua(target == "map" ? 1 : 0, rest.substr(sp + 1));
            return Result<void>::ok();
        }
        if (a == "run_file") {
            // The open file, or [from, to) of it as a selection would give.
            std::istringstream ws(rest);
            std::string target;
            int from = -1, to = -1;
            ws >> target >> from >> to;
            if (target != "game" && target != "map")
                return Result<void>::fail("usage: luaedit run_file <game|map> [from to]");
            if (e.rel.empty()) return Result<void>::fail("luaedit run_file: no file is open");
            e.sel_a = from >= 0 ? from : 0;
            e.sel_b = to > from ? std::min(to, static_cast<int>(e.text.size())) : e.sel_a;
            run_script(target == "map" ? 1 : 0);
            return Result<void>::ok();
        }
        if (a == "test") {
            const std::vector<TestMod> mods = test_mods();
            if (rest.empty()) {
                std::string ids;
                for (const TestMod& m : mods) ids += " " + m.id;
                ctx.out.info("luaedit: mods with tests:" + (ids.empty() ? std::string(" none") : ids));
                if (!e.test_reports.empty()) {
                    int passed = 0, failed = 0;
                    for (const auto& [file, r] : e.test_reports) passed += r.passed(), failed += r.failed();
                    ctx.out.info("luaedit: last run (" + e.tests_for + "): " + std::to_string(passed) + " passed, " +
                                 std::to_string(failed) + " failed");
                }
                return Result<void>::ok();
            }
            for (int i = 0; i < static_cast<int>(mods.size()); ++i)
                if (mods[static_cast<std::size_t>(i)].id == rest) e.test_mod = i;
            auto r = run_tests(rest);
            if (!r) return r;
            ctx.out.info("luaedit: running the tests of " + rest + " (results in the Tests tab and the log)");
            return Result<void>::ok();
        }
        if (a == "sig") {
            if (!e.call.in_call) return Result<void>::fail("luaedit sig: the caret is not in an argument list");
            const DocText doc = doc_for(e.call.name);
            if (!doc.found) return Result<void>::fail("luaedit sig: " + e.call.name + " is not described");
            const dt::LuaSignatureParts parts = dt::signature_parts(doc.signature);
            const int act = dt::active_parameter(parts, e.call.argument);
            std::string p = act < 0 ? std::string("none")
                                    : doc.signature.substr(
                                          static_cast<std::size_t>(parts.params[static_cast<std::size_t>(act)].first),
                                          static_cast<std::size_t>(parts.params[static_cast<std::size_t>(act)].second -
                                                                   parts.params[static_cast<std::size_t>(act)].first));
            ctx.out.info("luaedit: " + doc.signature + " ; parameter " + std::to_string(e.call.argument + 1) + ": " +
                         p + " ; " + doc.text());
            return Result<void>::ok();
        }
        if (a == "new") {
            std::istringstream ws(rest);
            std::string mod, kind, subject;
            ws >> mod >> kind >> subject;
            if (mod.empty() || kind.empty()) {
                std::string kinds;
                for (const dt::NewScriptKind& k : dt::new_script_kinds()) kinds += (kinds.empty() ? "" : ", ") + std::string(k.id);
                return Result<void>::fail("usage: luaedit new <mod> <kind> [map|character|name]; kinds: " + kinds);
            }
            return report(create_script(mod, kind, subject));
        }
        if (a == "caret") {
            int line = 1, column = 1;
            std::istringstream(rest) >> line >> column;
            go_to_line(line);
            const int l = line_of(e.cursor) - 1;
            const int end = l + 1 < static_cast<int>(e.line_start.size()) ? e.line_start[static_cast<std::size_t>(l + 1)] - 1
                                                                          : static_cast<int>(e.text.size());
            e.cursor = std::min(end, e.cursor + std::max(0, column - 1));
            e.pending_sel_a = e.pending_sel_b = e.cursor;
            return Result<void>::ok();
        }
        if (a == "type") {
            // As if typed at the caret ("\n" is a new line): the completion list opens as it does under the keys.
            std::string typed;
            for (std::size_t i = 0; i < rest.size(); ++i) {
                if (rest[i] == '\\' && i + 1 < rest.size() && rest[i + 1] == 'n') typed.push_back('\n'), ++i;
                else typed.push_back(rest[i]);
            }
            if (e.rel.empty()) return Result<void>::fail("luaedit type: no file is open");
            const int at = std::clamp(e.cursor, 0, static_cast<int>(e.text.size()));
            apply_edit(at, at, typed, at + static_cast<int>(typed.size()), false);
            relayout();
            update_completion(false);
            e.call = dt::call_at(e.text, e.cursor);
            e.call_cursor = e.cursor;
            ctx.out.info("luaedit: typed; " + std::to_string(e.comp.size()) + " completions" +
                         (e.call.name.empty() ? "" : ", at " + e.call.name));
            return Result<void>::ok();
        }
        if (a == "complete" || a == "accept") {
            if (a == "complete") update_completion(true);
            if (!e.comp_open) return Result<void>::fail("luaedit " + a + ": no completions at the caret");
            if (!rest.empty()) e.comp_sel = std::clamp(std::atoi(rest.c_str()), 0, static_cast<int>(e.comp.size()) - 1);
            e.comp_scroll = true;
            const dt::LuaCompletion picked = e.comp[static_cast<std::size_t>(e.comp_sel)];
            if (a == "accept") {
                accept_completion(e.comp_sel, false);
                relayout();
                ctx.out.info("luaedit: accepted " + picked.text);
                return Result<void>::ok();
            }
            std::string line = "luaedit: " + std::to_string(e.comp.size()) + " completions:";
            for (std::size_t i = 0; i < e.comp.size() && i < 12; ++i) line += " " + e.comp[i].text;
            ctx.out.info(line);
            const DocText doc = doc_for(picked.full);
            if (doc.found) ctx.out.info("luaedit: " + doc.signature + " : " + doc.text());
            return Result<void>::ok();
        }
        if (a == "api") {
            std::snprintf(e.api_filter, sizeof(e.api_filter), "%s", rest.c_str());
            e.left_tab = 1;
            e.api_view_dirty = true;
            ctx.out.info("luaedit: " + std::to_string(filtered_api().size()) + " of " + std::to_string(e.api_rows.size()) +
                         " functions match, " + std::to_string(e.api_described) + " described");
            return Result<void>::ok();
        }
        if (a == "api_insert") {
            if (e.names.find(rest) == nullptr) return Result<void>::fail("luaedit api_insert: the scripts have no " + rest);
            insert_call(rest);
            relayout();
            return Result<void>::ok();
        }
        if (a == "names") {
            // The listing the completion and the highlighter work from: "<origin> <kind> <name>" per line.
            std::string listing;
            for (const auto& [name, n] : e.names.all()) {
                listing += n.origin == dt::LuaOrigin::Engine ? "engine " : n.origin == dt::LuaOrigin::Library ? "library " : "script ";
                listing += n.kind == dt::LuaNameKind::Function ? "function " : n.kind == dt::LuaNameKind::Table ? "table " : "value ";
                listing += name + "\n";
            }
            if (!rest.empty()) {
                auto w = write_text(fs::path(rest), listing);
                if (!w) return w;
            }
            ctx.out.info("luaedit: " + std::to_string(e.names.size()) + " names" + (rest.empty() ? "" : " written to " + rest));
            return Result<void>::ok();
        }
        if (a == "docs") {
            // How much of the running API tools/lua_api.json describes, and its entries the scripts do not have.
            int functions = 0, described = 0, stale = 0;
            std::string missing, extra;
            for (const auto& [name, n] : e.names.all()) {
                if (n.kind != dt::LuaNameKind::Function || n.origin != dt::LuaOrigin::Engine) continue;
                ++functions;
                if (!doc_for(name).description.empty()) ++described;
                else missing += " " + name;
            }
            for (const auto& [name, d] : e.docs.all())
                if (e.names.find(name) == nullptr) ++stale, extra += " " + name;
            ctx.out.info("luaedit: " + std::to_string(described) + " of " + std::to_string(functions) +
                         " engine functions described; " + std::to_string(stale) + " described names the scripts do not have");
            if (!missing.empty()) SH_LOGF(GAME, SH_LOGLV_INFO, "[LUAEDIT] not described:%s", missing.c_str());
            if (!extra.empty()) SH_LOGF(GAME, SH_LOGLV_WARN, "[LUAEDIT] described but not in the scripts:%s", extra.c_str());
            return Result<void>::ok();
        }
        if (a == "wheel") {
            // A pointer move and a wheel turn through the window's event queue, as the mouse sends them: x and y
            // are fractions of the window. For scripted checks of what the wheel scrolls.
            float x = 0.5f, y = 0.5f, notches = -1.0f;
            std::istringstream(rest) >> x >> y >> notches;
            if (g_window == nullptr) return Result<void>::fail("luaedit wheel: no window");
            int w = 0, h = 0;
            SDL_GetWindowSize(g_window, &w, &h);
            SDL_Event motion{};
            motion.type = SDL_EVENT_MOUSE_MOTION;
            motion.motion.windowID = SDL_GetWindowID(g_window);
            motion.motion.x = x * static_cast<float>(w);
            motion.motion.y = y * static_cast<float>(h);
            SDL_PushEvent(&motion);
            SDL_Event wheel{};
            wheel.type = SDL_EVENT_MOUSE_WHEEL;
            wheel.wheel.windowID = motion.motion.windowID;
            wheel.wheel.y = notches;
            wheel.wheel.mouse_x = motion.motion.x;
            wheel.wheel.mouse_y = motion.motion.y;
            SDL_PushEvent(&wheel);
            return Result<void>::ok();
        }
        char scroll[160];
        std::snprintf(scroll, sizeof(scroll), " | scroll files %.0f api %.0f code %.0f window-range %.0f", e.scroll_files,
                      e.scroll_api, e.scroll_code, e.scroll_window_max);
        ctx.out.info("luaedit: " + (e.rel.empty() ? std::string("no file") : e.rel) + " [" +
                     (e.source_mod.empty() ? "base" : e.source_mod) + "] " + e.status + scroll);
        return Result<void>::ok();
    };
    (void)sh::console::global_registry().add(std::move(c));
}

struct Install {
    Install() {
        Panel p;
        p.id = "tools.lua_editor";
        p.title = "Lua editor";
        p.menu = "Data";
        p.draw = &draw_panel;
        p.open = false;
        p.auto_size = false;
        register_panel(std::move(p));
        register_luaedit_command();
        ShLog_SetListener(&log_listener);
    }
};

const Install s_install;

}  // namespace

Result<void> open_in_lua_editor(const fs::path& root, const std::string& rel, const std::string& mod) {
    return open_file(rel, mod, root);
}

}  // namespace sh::tools
