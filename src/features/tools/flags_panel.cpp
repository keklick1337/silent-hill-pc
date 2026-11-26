// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The modding tools' Flags panel: the event flags of the running game by name
// (gamedata/flags/*.json and the mods' flags) with a filter, set and cleared live, and the map variables of the
// current map (maps/<map>/map_vars.json, config/map_vars.json) read and written through the scripts' sh.map.get /
// sh.map.set. Console `flagpanel` drives it for scripted runs.

#include "features/tools/tools.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "engine/console/instance.h"
#include "engine/console/registry.h"
#include "engine/devtools/lua_ide.h"
#include "engine/game_state/game_state.h"
#include "engine/ids/game_numbers.h"
#include "engine/log/sh_log.h"
#include "engine/mods/registry.h"
#if defined(SH_LUA)
#include "engine/script/sh_script.h"
#endif

namespace sh::tools {

namespace {

namespace dt = sh::devtools;
namespace fs = std::filesystem;
namespace gs = sh::game_state;

struct FlagRow {
    int bit = 0;
    std::string name;   // full id
    std::string lower;  // for the filter
};

struct State {
    std::vector<FlagRow> flags;
    bool flags_valid = false;
    char filter[96] = "";
    bool only_set = false;

    std::string vars_map;  // the map `vars` were read for
    std::vector<dt::MapVarRow> vars;
    std::vector<std::string> keys, values;
    std::map<std::string, std::string> edits;  // key -> text being typed
    char var_filter[96] = "";
    bool reading = false;
    std::chrono::steady_clock::time_point read_at{};
    std::string var_status;
};

State& st() {
    static State s;
    return s;
}

constexpr int kMaxElements = 16;  // elements of an array variable shown

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string map_now() {
#if defined(SH_LUA)
    const char* m = ShScript_MapName();
    if (m != nullptr && *m != '\0') return m;
#endif
    const char* n = gs::map_name(gs::current_map());
    return n != nullptr ? n : "";
}

void load_flags() {
    State& s = st();
    s.flags.clear();
    for (int bit = 1; bit < sh::ids::flag_bit_end(); ++bit) {
        const char* n = sh::ids::flag_name(bit);
        if (n != nullptr) s.flags.push_back({bit, n, lower(n)});
    }
    std::sort(s.flags.begin(), s.flags.end(), [](const FlagRow& a, const FlagRow& b) { return a.name < b.name; });
    s.flags_valid = true;
}

// Sets or clears a flag in the next update, as an event would, and says so in the log.
void set_flag(const std::string& name, int bit, bool on) {
    defer([name, bit, on] {
        gs::set_event_flag(bit, on);
        SH_LOGF(GAME, SH_LOGLV_INFO, "[FLAGS] %s %s (bit %d) from the Flags panel", on ? "set" : "cleared",
                name.c_str(), bit);
    });
}

const FlagRow* find_flag(const std::string& name) {
    const std::string l = lower(name);
    for (const FlagRow& f : st().flags)
        if (f.lower == l || f.lower == "silenthill:" + l) return &f;
    return nullptr;
}

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The map's own map_vars.json first (the mods' copies before the base's), then config/map_vars.json the same way.
void load_vars(const std::string& map) {
    State& s = st();
    s.vars.clear();
    s.edits.clear();
    s.vars_map = map;
    const auto& reg = sh::mods::registry();
    std::vector<fs::path> roots;
    const auto active = reg.active();
    for (auto it = active.rbegin(); it != active.rend(); ++it) roots.push_back((*it)->candidate.root);
    roots.push_back(reg.config().base_dir);
    for (const fs::path& rel : {fs::path("maps") / map / "map_vars.json", fs::path("config") / "map_vars.json"}) {
        for (const fs::path& root : roots) {
            std::error_code ec;
            if (fs::is_regular_file(root / rel, ec)) dt::read_map_var_rows(read_file(root / rel), s.vars);
        }
    }
    s.keys = dt::map_var_keys(s.vars, kMaxElements);
    s.values.assign(s.keys.size(), "");
}

// Reads every value in one snippet, in the next update (the game script's state, else the map script's).
void read_values() {
#if defined(SH_LUA)
    State& s = st();
    if (s.reading || s.keys.empty()) return;
    s.reading = true;
    defer([] {
        State& s = st();
        static char out[65536];
        const std::string code = dt::map_var_read_snippet(s.keys);
        int ok = ShScript_RunSnippet(0, code.c_str(), out, sizeof(out));
        if (!ok) ok = ShScript_RunSnippet(1, code.c_str(), out, sizeof(out));
        s.values = ok ? dt::parse_map_var_values(out, s.keys.size()) : std::vector<std::string>(s.keys.size(), "?");
        s.var_status = ok ? "" : std::string("cannot read: ") + out;
        s.reading = false;
        s.read_at = std::chrono::steady_clock::now();
    });
#endif
}

Result<void> write_var(const std::string& key, const std::string& value) {
    const std::string code = dt::map_var_write_snippet(key, value);
    if (code.empty()) return Result<void>::fail("not a number or true/false: " + value);
#if defined(SH_LUA)
    defer([key, code] {
        static char out[1024];
        int ok = ShScript_RunSnippet(0, code.c_str(), out, sizeof(out));
        if (!ok) ok = ShScript_RunSnippet(1, code.c_str(), out, sizeof(out));
        SH_LOGF(GAME, ok ? SH_LOGLV_INFO : SH_LOGLV_WARN, "[FLAGS] map variable %s: %s%s", key.c_str(), code.c_str(),
                ok ? "" : (std::string(" failed: ") + out).c_str());
    });
    read_values();
    return Result<void>::ok();
#else
    return Result<void>::fail("no Lua in this build");
#endif
}

void draw_flags_tab() {
    State& s = st();
    if (!s.flags_valid) load_flags();
    if (gs::save_event_flags() == nullptr) {
        ImGui::TextDisabled("No game is running yet: the flags live in the save.");
        return;
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
    ImGui::InputTextWithHint("##flagfilter", "filter (M1S03, Shotgun, mymod:)", s.filter, sizeof(s.filter));
    ImGui::SameLine();
    ImGui::Checkbox("only set", &s.only_set);
    ImGui::SameLine();
    if (ImGui::Button("Reload names")) load_flags();
    const std::string f = lower(s.filter);
    std::vector<const FlagRow*> rows;
    int set_count = 0;
    for (const FlagRow& r : s.flags) {
        const bool on = gs::event_flag(r.bit);
        set_count += on ? 1 : 0;
        if ((!s.only_set || on) && (f.empty() || r.lower.find(f) != std::string::npos)) rows.push_back(&r);
    }
    ImGui::TextDisabled("%d named flags, %d set; %d shown. A click sets or clears one now.",
                        static_cast<int>(s.flags.size()), set_count, static_cast<int>(rows.size()));
    ImGui::BeginChild("##flags", ImVec2(0, 0), ImGuiChildFlags_Borders);
    ImGuiListClipper clip;
    clip.Begin(static_cast<int>(rows.size()));
    while (clip.Step()) {
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            const FlagRow& r = *rows[static_cast<std::size_t>(i)];
            bool on = gs::event_flag(r.bit);
            ImGui::PushID(r.bit);
            if (ImGui::Checkbox(r.name.c_str(), &on)) set_flag(r.name, r.bit, on);
            ImGui::SameLine();
            ImGui::TextDisabled("bit %d", r.bit);
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

void draw_vars_tab() {
    State& s = st();
    const std::string map = map_now();
    if (map != s.vars_map) load_vars(map);
    if (s.vars.empty()) {
        ImGui::TextDisabled("%s has no map variables (maps/%s/map_vars.json, config/map_vars.json).",
                            map.empty() ? "No map" : map.c_str(), map.c_str());
        return;
    }
    if (std::chrono::steady_clock::now() - s.read_at > std::chrono::milliseconds(500)) read_values();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
    ImGui::InputTextWithHint("##varfilter", "filter", s.var_filter, sizeof(s.var_filter));
    ImGui::SameLine();
    ImGui::TextDisabled("%s: %d variables. Enter in a value writes it (sh.map.set).", map.c_str(),
                        static_cast<int>(s.vars.size()));
    if (!s.var_status.empty()) ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.45f, 1.0f), "%s", s.var_status.c_str());
    const std::string f = lower(s.var_filter);
    ImGui::BeginChild("##vars", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("##vartable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch, 0.45f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.25f);
        ImGui::TableSetupColumn("where", ImGuiTableColumnFlags_WidthStretch, 0.30f);
        std::size_t k = 0;
        for (const dt::MapVarRow& v : s.vars) {
            const int n = v.count <= 1 ? 1 : std::min(v.count, kMaxElements);
            const bool shown = f.empty() || lower(v.name).find(f) != std::string::npos;
            for (int i = 0; i < n; ++i, ++k) {
                if (!shown || k >= s.keys.size()) continue;
                const std::string& key = s.keys[k];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(key.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::PushID(static_cast<int>(k));
                const bool editing = s.edits.count(key) != 0;
                std::string& text = s.edits[key];
                if (!editing) text = k < s.values.size() ? s.values[k] : "";
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%s", text.c_str());
                ImGui::SetNextItemWidth(-1);
                ImGui::BeginDisabled(v.type == "ref");
                const bool enter = ImGui::InputText("##v", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::EndDisabled();
                text = buf;
                if (enter) {
                    auto r = write_var(key, text);
                    s.var_status = r ? "" : r.error();
                }
                if (!ImGui::IsItemActive() && !enter) s.edits.erase(key);
                ImGui::PopID();
                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled("%s %s", v.type.c_str(), v.where.c_str());
            }
            if (v.count > kMaxElements && shown) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("  ... %d more elements of %s", v.count - kMaxElements, v.name.c_str());
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void draw_panel() {
    static int s_placed = 0;
    if (s_placed < 3) {
        ++s_placed;
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::SetWindowSize(ImVec2(io.DisplaySize.x * 0.34f, io.DisplaySize.y * 0.6f));
    }
    if (ImGui::BeginTabBar("##flagtabs")) {
        if (ImGui::BeginTabItem("Event flags")) {
            draw_flags_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Map variables")) {
            draw_vars_tab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

// --- console: `flagpanel`, the panel's actions for scripted runs ---

void register_command() {
    using sh::console::ArgSpec;
    using sh::console::ArgType;
    sh::console::CommandSpec c;
    c.name = "flagpanel";
    c.group = "debug";
    c.help = "drive the editor's Flags panel: list [filter], toggle <flag>, vars, setvar <name[i]> <value>";
    c.args = {ArgSpec{"action", ArgType::Enum, false, "what to do", {"list", "toggle", "vars", "setvar"}, {}},
              ArgSpec{"rest", ArgType::Rest, true, "the action's arguments", {}, {}}};
    c.details = "docs/modding/tools.md. Panel id tools.flags (editorpanel tools.flags on).";
    c.run = [](sh::console::Context& ctx, const sh::console::Args& args) -> Result<void> {
        State& s = st();
        if (!s.flags_valid) load_flags();
        const std::string& a = args.str(0);
        const std::string rest = args.joined(1);
        if (a == "list") {
            const std::string f = lower(rest);
            int shown = 0, set = 0;
            std::string line;
            for (const FlagRow& r : s.flags) {
                if (!f.empty() && r.lower.find(f) == std::string::npos) continue;
                ++shown;
                if (!gs::event_flag(r.bit)) continue;
                ++set;
                if (line.size() < 900) line += " " + r.name;
            }
            ctx.out.info("flagpanel: " + std::to_string(shown) + " flags, " + std::to_string(set) + " set:" + line);
            return Result<void>::ok();
        }
        if (a == "toggle") {
            const FlagRow* r = find_flag(rest);
            if (r == nullptr) return Result<void>::fail("flagpanel toggle: no flag named " + rest);
            if (gs::save_event_flags() == nullptr) return Result<void>::fail("flagpanel toggle: no game is running");
            const bool on = !gs::event_flag(r->bit);
            set_flag(r->name, r->bit, on);
            ctx.out.info("flagpanel: " + std::string(on ? "setting " : "clearing ") + r->name);
            return Result<void>::ok();
        }
        if (a == "vars") {
            if (map_now() != s.vars_map) load_vars(map_now());
            if (!s.var_status.empty()) ctx.out.info("flagpanel: " + s.var_status);
            std::string line;
            for (std::size_t i = 0; i < s.keys.size() && line.size() < 900; ++i)
                line += " " + s.keys[i] + "=" + (i < s.values.size() ? s.values[i] : "");
            read_values();
            ctx.out.info("flagpanel: " + s.vars_map + " " + std::to_string(s.vars.size()) +
                         " variables (values as of "
                         "the last read):" +
                         line);
            return Result<void>::ok();
        }
        const std::size_t sp = rest.find(' ');
        if (sp == std::string::npos) return Result<void>::fail("usage: flagpanel setvar <name[i]> <value>");
        return write_var(rest.substr(0, sp), rest.substr(sp + 1));
    };
    (void)sh::console::global_registry().add(std::move(c));
}

struct Install {
    Install() {
        Panel p;
        p.id = "tools.flags";
        p.title = "Flags";
        p.menu = "Data";
        p.draw = &draw_panel;
        p.open = false;
        p.auto_size = false;
        register_panel(std::move(p));
        register_command();
    }
};

const Install s_install;

}  // namespace

}  // namespace sh::tools
