// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The modding tools' Mod panel (docs/modding/editor.md): a new mod from a name, objects placed on the current map
// (message and script triggers, character spawns, item pickups), moved with the arrow keys, sliders or typed
// numbers, drawn in the world, and saved to the mod's files (engine/devtools/mod_authoring.h). Console `modnew` and
// `modobj` drive it for scripted runs.

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <sstream>
#include <string>
#include <vector>

#include "engine/console/instance.h"
#include "engine/console/registry.h"
#include "engine/devtools/mod_authoring.h"
#include "engine/game_state/game_state.h"
#include "engine/mods/manifest.h"
#include "engine/mods/registry.h"
#include "engine/version/engine_version.h"
#include "features/tools/tools.h"
#include "features/tools/tools_internal.h"
#if defined(SH_LUA)
#include "engine/script/sh_script.h"
#endif

namespace sh::tools {

namespace {

namespace dt = sh::devtools;
namespace fs = std::filesystem;
namespace gs = sh::game_state;

constexpr const char* kKinds[] = {"message", "script", "spawn", "pickup"};
constexpr const char* kActivations[] = {"enter", "action", "load"};
constexpr const char* kAis[] = {"auto", "full", "fallback", "none"};

struct State {
    std::string mod;  // "" until a mod is made or opened
    fs::path root;
    std::string map;
    std::vector<dt::ModObject> objects;
    int selected = -1;
    bool dirty = false;
    std::string status;
    bool status_error = false;

    char new_id[65] = "";
    char new_name[96] = "";
    char open_id[65] = "";
    char text[1024] = "";
    char chara_filter[48] = "";
    char flag[96] = "";
    int text_for = -1;  // the object `text` and `flag` were copied from

    float step = 0.1f;  // metres per arrow press
    bool strong = false;
    bool arrows = true;
    int place_kind = -1;  // armed "click to place" (an index of kKinds)

    std::vector<std::string> charas, items;
};

State& st() {
    static State s;
    return s;
}

void set_status(std::string text, bool error) {
    State& s = st();
    s.status = std::move(text);
    s.status_error = error;
}

std::string map_now() {
#if defined(SH_LUA)
    const char* m = ShScript_MapName();
    if (m != nullptr && *m != '\0') return m;
#endif
    const char* n = gs::map_name(gs::current_map());
    return n != nullptr ? n : "";
}

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

Result<void> write_file(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    if (!out) return Result<void>::fail("cannot write " + p.string());
    return Result<void>::ok();
}

fs::path mods_dir() {
    const auto& dirs = sh::mods::registry().config().dirs;
    return dirs.empty() ? fs::path() : dirs.front();
}

void list_names(const fs::path& dir, bool folders, std::vector<std::string>& out) {
    out.clear();
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (folders && e.is_directory()) {
            out.push_back(e.path().filename().string());
        } else if (!folders && e.path().extension() == ".json") {
            out.push_back(e.path().stem().string());
        }
    }
    std::sort(out.begin(), out.end());
}

void load_lists() {
    State& s = st();
    if (!s.charas.empty()) return;
    const fs::path base = sh::mods::registry().config().base_dir;
    list_names(base / "chara", true, s.charas);
    list_names(base / "items", false, s.items);
}

fs::path objects_path(const std::string& map) { return st().root / "maps" / map / "editor_objects.json"; }

void load_objects() {
    State& s = st();
    s.map = map_now();
    s.objects.clear();
    s.selected = -1;
    s.dirty = false;
    s.text_for = -1;
    std::error_code ec;
    const fs::path p = objects_path(s.map);
    if (s.mod.empty() || !fs::is_regular_file(p, ec)) return;
    auto parsed = dt::parse_map_objects(read_file(p));
    if (!parsed) return set_status(p.string() + ": " + parsed.error(), true);
    s.objects = std::move(parsed.value().objects);
    if (!s.objects.empty()) s.selected = 0;
}

Result<void> open_mod(const std::string& id) {
    State& s = st();
    const fs::path root = mods_dir() / id;
    std::error_code ec;
    if (!fs::is_regular_file(root / "mod.json", ec)) return Result<void>::fail("no mod folder " + root.string());
    s.mod = id;
    s.root = root;
    load_objects();
    set_status("editing mod " + id + " on " + s.map + " (" + std::to_string(s.objects.size()) + " objects)", false);
    return Result<void>::ok();
}

Result<void> new_mod(const std::string& id, const std::string& name) {
    if (!sh::mods::valid_mod_id(id) || id == "silenthill")
        return Result<void>::fail("bad mod id '" + id + "' (letters, digits, _ - .)");
    const fs::path dir = mods_dir();
    if (dir.empty()) return Result<void>::fail("no mods folder");
    const fs::path root = dir / id;
    std::error_code ec;
    if (fs::exists(root, ec)) return Result<void>::fail("mods/" + id + " already exists (modobj open " + id + ")");
    if (auto w = write_file(root / "mod.json", dt::new_manifest(id, name, sh::version::kText)); !w) return w;
    // Switched on in mods/load_order.json, so the next start runs it.
    sh::mods::Registry& reg = sh::mods::registry();
    reg.rescan_pending();
    const bool on = reg.set_enabled(id, true) && static_cast<bool>(reg.save());
    if (auto r = open_mod(id); !r) return r;
    set_status(
        "made mods/" + id + " for " + st().map + (on ? ", switched on (restart to run it)" : ", not switched on"), !on);
    return Result<void>::ok();
}

bool mod_running() {
    State& s = st();
    return !s.mod.empty() && sh::mods::registry().plan().find(s.mod) != nullptr;
}

std::string unique_id(const std::string& base) {
    const State& s = st();
    for (int n = 1;; ++n) {
        const std::string id = base + std::to_string(n);
        if (std::none_of(s.objects.begin(), s.objects.end(), [&](const dt::ModObject& o) { return o.id == id; }))
            return id;
    }
}

std::int32_t yaw_q12(double degrees) {
    double d = std::fmod(degrees, 360.0);
    if (d < 0) d += 360.0;
    return static_cast<std::int32_t>(d / 360.0 * 4096.0) & 4095;
}

double yaw_degrees(std::int32_t q) { return (q & 4095) * 360.0 / 4096.0; }

Vec3 player_metres() {
    const auto p = gs::chara_position(0);
    return {metres(p.x), metres(p.y), metres(p.z)};
}

Result<void> add_object(dt::ModObjectKind kind, const Vec3& at, std::int32_t yaw, const std::string& id_in) {
    State& s = st();
    if (s.mod.empty()) return Result<void>::fail("make or open a mod first");
    if (s.map != map_now()) load_objects();
    const std::string id = id_in.empty() ? unique_id(dt::mod_object_kind_name(kind)) : id_in;
    if (!dt::valid_object_id(id)) return Result<void>::fail("bad object id '" + id + "'");
    for (const dt::ModObject& o : s.objects)
        if (o.id == id) return Result<void>::fail("object " + id + " exists");
    load_lists();
    dt::ModObject o;
    o.id = id;
    o.kind = kind;
    o.x = q12(at.x);
    o.y = q12(at.y);
    o.z = q12(at.z);
    o.yaw = yaw;
    if (kind == dt::ModObjectKind::Message) o.text = "Something is written here.";
    if (kind == dt::ModObjectKind::Spawn) {
        const bool groaner = std::find(s.charas.begin(), s.charas.end(), "Groaner") != s.charas.end();
        o.chara = groaner ? "Groaner" : (s.charas.empty() ? "" : s.charas.front());
        o.ai = "full";
    }
    if (kind == dt::ModObjectKind::Pickup) o.item = "HealthDrink";
    s.objects.push_back(std::move(o));
    s.selected = static_cast<int>(s.objects.size()) - 1;
    s.dirty = true;
    set_status("added " + std::string(dt::mod_object_kind_name(kind)) + " " + id, false);
    return Result<void>::ok();
}

std::vector<dt::ModMapObjects> all_maps() {
    State& s = st();
    std::vector<dt::ModMapObjects> maps;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(s.root / "maps", ec)) {
        if (!e.is_directory() || e.path().filename().string() == s.map) continue;
        const fs::path p = e.path() / "editor_objects.json";
        if (!fs::is_regular_file(p, ec)) continue;
        auto parsed = dt::parse_map_objects(read_file(p));
        if (parsed) maps.push_back(std::move(parsed.value()));
    }
    maps.push_back({s.map, s.objects});
    std::sort(maps.begin(), maps.end(), [](const auto& a, const auto& b) { return a.map < b.map; });
    return maps;
}

// Writes the file, or removes it when the text is empty.
Result<void> put(const fs::path& p, const std::string& text) {
    if (!text.empty()) return write_file(p, text);
    std::error_code ec;
    fs::remove(p, ec);
    return Result<void>::ok();
}

Result<void> save() {
    State& s = st();
    if (s.mod.empty()) return Result<void>::fail("no mod");
    const dt::ModMapObjects here{s.map, s.objects};
    const std::vector<dt::ModMapObjects> maps = all_maps();
    std::string region = dt::runtime_region(s.mod, maps);
    const fs::path main = s.root / "scripts" / "main.lua";
    std::error_code ec;
    const std::string main_text = dt::merge_main_lua(fs::is_regular_file(main, ec) ? read_file(main) : "", region);
    for (const auto& [path, text] :
         {std::pair{objects_path(s.map), s.objects.empty() ? std::string() : dt::write_map_objects(here)},
          std::pair{s.root / "text" / "en" / "editor" / (s.map + ".json"), dt::write_text_doc(s.mod, here)},
          std::pair{s.root / "flags" / "editor.json", dt::write_flags_doc(s.mod, maps)}, std::pair{main, main_text}}) {
        if (auto r = put(path, text); !r) return r;
    }
    s.dirty = false;
    set_status("saved " + std::to_string(s.objects.size()) + " objects of " + s.map + " to mods/" + s.mod, false);
    return Result<void>::ok();
}

Result<void> create_script(dt::ModObject& o) {
    State& s = st();
    if (o.kind != dt::ModObjectKind::Script) return Result<void>::fail(o.id + " is not a script trigger");
    const std::string rel = "maps/" + s.map + "/hooks.lua";
    const fs::path p = s.root / rel;
    std::error_code ec;
    const std::string before = fs::is_regular_file(p, ec) ? read_file(p) : std::string();
    const std::string after = dt::add_script_stub(before, s.mod, s.map, o.id);
    if (after != before)
        if (auto w = write_file(p, after); !w) return w;
    if (auto r = open_in_lua_editor(s.root, rel, s.mod); !r) return r;
    run_command("editorpanel tools.lua_editor on");
    set_status("the handler of " + o.id + " is in " + rel, false);
    return Result<void>::ok();
}

void reload() {
    State& s = st();
    if (s.dirty)
        if (auto r = save(); !r) return set_status(r.error(), true);
    if (mod_running()) {
        run_command("mods reload");
        set_status("mods reload: the map loads again with the saved objects", false);
    } else {
        set_status("mods/" + s.mod + " is not running yet: restart the game (console `restart`) to load it", false);
    }
}

void nudge(int axis, double metres_by) {
    State& s = st();
    if (s.selected < 0 || s.selected >= static_cast<int>(s.objects.size())) return;
    dt::ModObject& o = s.objects[static_cast<std::size_t>(s.selected)];
    std::int32_t* v = axis == 0 ? &o.x : axis == 1 ? &o.y : &o.z;
    *v += q12(metres_by);
    s.dirty = true;
}

// --- panel ---

bool edit_metres(const char* label, std::int32_t* q, float speed) {
    double m = metres(*q);
    if (!ImGui::DragScalar(label, ImGuiDataType_Double, &m, speed, nullptr, nullptr, "%.3f m")) return false;
    *q = q12(m);
    return true;
}

void draw_object(dt::ModObject& o) {
    State& s = st();
    const float speed = s.strong ? 0.1f : 0.005f;
    bool changed = false;
    ImGui::Text("%s  (%s)", o.id.c_str(), dt::mod_object_kind_name(o.kind));
    changed |= edit_metres("x", &o.x, speed);
    changed |= edit_metres("y", &o.y, speed);
    changed |= edit_metres("z", &o.z, speed);
    double yaw = yaw_degrees(o.yaw);
    if (ImGui::DragScalar("yaw", ImGuiDataType_Double, &yaw, s.strong ? 5.0f : 0.5f, nullptr, nullptr, "%.1f deg")) {
        o.yaw = yaw_q12(yaw);
        changed = true;
    }
    if (ImGui::SmallButton("To player")) {
        const Vec3 p = player_metres();
        o.x = q12(p.x), o.y = q12(p.y), o.z = q12(p.z);
        o.yaw = gs::chara_rotation(0).y & 4095;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("To camera")) {
        const Vec3 c = camera_position();
        o.x = q12(c.x), o.y = q12(c.y), o.z = q12(c.z);
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("To last pick") && last_pick().hit) {
        o.x = q12(last_pick().point.x), o.y = q12(last_pick().point.y), o.z = q12(last_pick().point.z);
        changed = true;
    }
    if (o.kind != dt::ModObjectKind::Pickup) {
        changed |= edit_metres("radius", &o.radius, speed);
        changed |= edit_metres("height", &o.height, speed);
        int a = static_cast<int>(o.activation);
        const int n = o.kind == dt::ModObjectKind::Spawn ? 3 : 2;
        if (ImGui::Combo("activation", &a, kActivations, n)) {
            o.activation = static_cast<dt::ModActivation>(a);
            changed = true;
        }
        changed |= ImGui::Checkbox("once per playthrough", &o.once);
    }
    if (s.text_for != s.selected) {
        std::snprintf(s.text, sizeof s.text, "%s", o.text.c_str());
        std::snprintf(s.flag, sizeof s.flag, "%s", o.flag.c_str());
        s.text_for = s.selected;
    }
    if (ImGui::InputTextWithHint("flag", "condition: a flag name (empty: none)", s.flag, sizeof s.flag)) {
        o.flag = s.flag;
        changed = true;
    }
    if (!o.flag.empty()) changed |= ImGui::Checkbox("the flag must be set", &o.flag_value);
    switch (o.kind) {
        case dt::ModObjectKind::Message:
            if (ImGui::InputTextMultiline("text (English)", s.text, sizeof s.text, ImVec2(0, 70))) {
                o.text = s.text;
                changed = true;
            }
            break;
        case dt::ModObjectKind::Script:
            if (ImGui::Button("Create script")) {
                if (s.dirty) (void)save();
                if (auto r = create_script(o); !r) set_status(r.error(), true);
            }
            break;
        case dt::ModObjectKind::Spawn: {
            load_lists();
            ImGui::InputTextWithHint("##cf", "filter characters", s.chara_filter, sizeof s.chara_filter);
            if (ImGui::BeginCombo("character", o.chara.c_str())) {
                for (const std::string& c : s.charas) {
                    if (s.chara_filter[0] != '\0' && c.find(s.chara_filter) == std::string::npos) continue;
                    if (ImGui::Selectable(c.c_str(), c == o.chara)) o.chara = c, changed = true;
                }
                ImGui::EndCombo();
            }
            int ai = static_cast<int>(std::find(std::begin(kAis), std::end(kAis), o.ai) - std::begin(kAis));
            if (ImGui::Combo("ai", &ai, kAis, 4)) o.ai = kAis[std::clamp(ai, 0, 3)], changed = true;
            break;
        }
        case dt::ModObjectKind::Pickup:
            load_lists();
            if (ImGui::BeginCombo("item", o.item.c_str())) {
                for (const std::string& i : s.items)
                    if (ImGui::Selectable(i.c_str(), i == o.item)) o.item = i, changed = true;
                ImGui::EndCombo();
            }
            changed |= ImGui::InputInt("count (0: the game's)", &o.count);
            o.count = std::clamp(o.count, 0, 255);
            break;
    }
    if (changed) s.dirty = true;
}

void draw_panel() {
    State& s = st();
    if (!s.mod.empty() && s.map != map_now()) {
        if (s.dirty) (void)save();
        load_objects();
    }
    if (s.mod.empty()) {
        ImGui::TextUnformatted("New mod");
        ImGui::InputTextWithHint("id##new", "mymod", s.new_id, sizeof s.new_id);
        ImGui::InputTextWithHint("name##new", "My mod", s.new_name, sizeof s.new_name);
        if (ImGui::Button("Create")) {
            if (auto r = new_mod(s.new_id, s.new_name); !r) set_status(r.error(), true);
        }
        ImGui::Separator();
        ImGui::InputTextWithHint("##open", "id of a mod in mods/", s.open_id, sizeof s.open_id);
        ImGui::SameLine();
        if (ImGui::Button("Open")) {
            if (auto r = open_mod(s.open_id); !r) set_status(r.error(), true);
        }
    } else {
        ImGui::Text("mods/%s on %s%s%s", s.mod.c_str(), s.map.c_str(), s.dirty ? "  (unsaved)" : "",
                    mod_running() ? "" : "  (not running)");
        if (ImGui::Button("Save")) {
            if (auto r = save(); !r) set_status(r.error(), true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload mod")) reload();
        ImGui::SameLine();
        if (ImGui::Button("Close")) s = State{};
        ImGui::Separator();
        ImGui::TextUnformatted("Add:");
        for (int k = 0; k < 4; ++k) {
            ImGui::PushID(k);
            ImGui::SameLine();
            const bool armed = s.place_kind == k;
            if (armed) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.4f, 0.1f, 1));
            if (ImGui::SmallButton(kKinds[k])) s.place_kind = armed ? -1 : k;
            if (armed) ImGui::PopStyleColor();
            ImGui::PopID();
        }
        if (s.place_kind >= 0) {
            ImGui::TextDisabled("click the world to place a %s, or:", kKinds[s.place_kind]);
            const auto kind = static_cast<dt::ModObjectKind>(s.place_kind);
            if (ImGui::SmallButton("at player")) {
                (void)add_object(kind, player_metres(), gs::chara_rotation(0).y & 4095, "");
                s.place_kind = -1;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("at camera")) {
                double yaw = 0, pitch = 0;
                camera_angles(&yaw, &pitch);
                (void)add_object(kind, camera_position(), yaw_q12(yaw), "");
                s.place_kind = -1;
            }
        }
        ImGui::SliderFloat("step", &s.step, 0.01f, 2.0f, "%.2f m", ImGuiSliderFlags_Logarithmic);
        ImGui::Checkbox("arrow keys move", &s.arrows);
        ImGui::SameLine();
        ImGui::Checkbox("strong drag", &s.strong);
        ImGui::Separator();
        int remove = -1;
        if (ImGui::BeginListBox("##objects", ImVec2(-1, 6 * ImGui::GetTextLineHeightWithSpacing()))) {
            for (int i = 0; i < static_cast<int>(s.objects.size()); ++i) {
                const dt::ModObject& o = s.objects[static_cast<std::size_t>(i)];
                const std::string row = o.id + "  " + dt::mod_object_kind_name(o.kind);
                if (ImGui::Selectable(row.c_str(), i == s.selected)) s.selected = i;
            }
            ImGui::EndListBox();
        }
        if (s.selected >= 0 && s.selected < static_cast<int>(s.objects.size())) {
            if (ImGui::SmallButton("Delete")) remove = s.selected;
            ImGui::SameLine();
            if (ImGui::SmallButton("Player here")) {
                const dt::ModObject& o = s.objects[static_cast<std::size_t>(s.selected)];
                char line[96];
                std::snprintf(line, sizeof line, "tp %.3f %.3f %.3f", metres(o.x), metres(o.y), metres(o.z));
                run_command(line);
            }
            draw_object(s.objects[static_cast<std::size_t>(s.selected)]);
        }
        if (remove >= 0) {
            s.objects.erase(s.objects.begin() + remove);
            s.selected = std::min(remove, static_cast<int>(s.objects.size()) - 1);
            s.text_for = -1;
            s.dirty = true;
        }
        if (s.arrows && !ImGui::GetIO().WantTextInput) {
            const double d = s.step;
            const bool shift = ImGui::GetIO().KeyShift;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) nudge(0, -d);
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) nudge(0, d);
            // Shift: up and down (y grows downwards).
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) shift ? nudge(1, -d) : nudge(2, d);
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) shift ? nudge(1, d) : nudge(2, -d);
        }
    }
    if (!s.status.empty()) {
        ImGui::Separator();
        if (s.status_error) {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", s.status.c_str());
        } else {
            ImGui::TextWrapped("%s", s.status.c_str());
        }
    }
}

void draw_overlay(Draw3D& d) {
    State& s = st();
    if (s.mod.empty() || s.map != map_now()) return;
    static const Color kColors[] = {{1, 0.9f, 0.3f, 1}, {0.4f, 0.8f, 1, 1}, {1, 0.35f, 0.35f, 1}, {0.4f, 1, 0.5f, 1}};
    for (int i = 0; i < static_cast<int>(s.objects.size()); ++i) {
        const dt::ModObject& o = s.objects[static_cast<std::size_t>(i)];
        const Vec3 p{metres(o.x), metres(o.y), metres(o.z)};
        const Color c = kColors[static_cast<int>(o.kind)];
        const bool sel = i == s.selected;
        d.cross(p, sel ? 0.4 : 0.25, c, sel ? 3.0f : 1.5f);
        if (o.kind != dt::ModObjectKind::Pickup) {
            d.circle_xz(p, metres(o.radius), c, 40, sel ? 2.5f : 1.5f);
            d.line(p, {p.x, p.y - metres(o.height), p.z}, c);
        }
        const double a = o.yaw * 2.0 * std::numbers::pi / 4096.0;
        d.line(p, {p.x + std::sin(a) * 0.6, p.y, p.z + std::cos(a) * 0.6}, c, 2.0f);
        if (sel) {
            d.line(p, {p.x + 1, p.y, p.z}, {1, 0.2f, 0.2f, 1}, 2.5f);
            d.line(p, {p.x, p.y - 1, p.z}, {0.2f, 1, 0.2f, 1}, 2.5f);
            d.line(p, {p.x, p.y, p.z + 1}, {0.3f, 0.5f, 1, 1}, 2.5f);
        }
        d.label({p.x, p.y - 0.5, p.z}, o.id + " (" + dt::mod_object_kind_name(o.kind) + ")", c);
    }
}

bool on_pick(const Pick& pick) {
    State& s = st();
    if (s.place_kind < 0 || !pick.hit || pick.button != 0) return false;
    double yaw = 0, pitch = 0;
    camera_angles(&yaw, &pitch);
    if (auto r = add_object(static_cast<dt::ModObjectKind>(s.place_kind), pick.point, yaw_q12(yaw + 180.0), ""); !r)
        set_status(r.error(), true);
    s.place_kind = -1;
    return true;
}

// --- console: `modnew` and `modobj` ---

Result<void> report(sh::console::Context& ctx, Result<void> r) {
    if (r) ctx.out.info("modobj: " + st().status);
    return r;
}

Result<void> set_field(dt::ModObject& o, const std::string& key, const std::string& value) {
    auto as_int = [&](std::int32_t* out) {
        *out = q12(std::strtod(value.c_str(), nullptr));
        return Result<void>::ok();
    };
    if (key == "x") return as_int(&o.x);
    if (key == "y") return as_int(&o.y);
    if (key == "z") return as_int(&o.z);
    if (key == "radius") return as_int(&o.radius);
    if (key == "height") return as_int(&o.height);
    if (key == "yaw") return o.yaw = yaw_q12(std::strtod(value.c_str(), nullptr)), Result<void>::ok();
    if (key == "activation") {
        if (!dt::parse_mod_activation(value, &o.activation))
            return Result<void>::fail("activation: enter, action, load");
        return Result<void>::ok();
    }
    if (key == "once") return o.once = value == "1" || value == "true", Result<void>::ok();
    if (key == "flag") return o.flag = value == "-" ? "" : value, Result<void>::ok();
    if (key == "flag_value") return o.flag_value = value == "1" || value == "true", Result<void>::ok();
    if (key == "text") return o.text = value, Result<void>::ok();
    if (key == "chara") return o.chara = value, Result<void>::ok();
    if (key == "ai") return o.ai = value, Result<void>::ok();
    if (key == "item") return o.item = value, Result<void>::ok();
    if (key == "count") return o.count = std::clamp(std::atoi(value.c_str()), 0, 255), Result<void>::ok();
    return Result<void>::fail("unknown field " + key);
}

void register_commands() {
    using sh::console::ArgSpec;
    using sh::console::ArgType;
    sh::console::Registry& reg = sh::console::global_registry();

    sh::console::CommandSpec n;
    n.name = "modnew";
    n.group = "debug";
    n.help = "the editor's Mod panel: make mods/<id> with a mod.json, switched on, bound to the current map";
    n.args = {ArgSpec{"id", ArgType::String, false, "the mod id", {}, {}},
              ArgSpec{"name", ArgType::Rest, true, "the name on the mods screen", {}, {}}};
    n.details = "docs/modding/editor.md. Then modobj add/set/save.";
    n.run = [](sh::console::Context& ctx, const sh::console::Args& args) -> Result<void> {
        return report(ctx, new_mod(args.str(0), args.joined(1)));
    };
    (void)reg.add(std::move(n));

    sh::console::CommandSpec c;
    c.name = "modobj";
    c.group = "debug";
    c.help =
        "the editor's Mod panel: open <mod>, add <kind> [player|camera|pick|x y z] [id], select <id>, move <dx> <dy> "
        "<dz>, set <field> <value>, del, list, save, script, reload";
    c.args = {ArgSpec{"action",
                      ArgType::Enum,
                      false,
                      "what to do",
                      {"open", "add", "select", "move", "set", "del", "list", "save", "script", "reload"},
                      {}},
              ArgSpec{"rest", ArgType::Rest, true, "the action's arguments", {}, {}}};
    c.details =
        "docs/modding/editor.md. Kinds: message, script, spawn, pickup. move and the x, y, z, radius, height fields "
        "are metres, yaw degrees; fields: activation, once, flag (- clears), flag_value, text, chara, ai, item, "
        "count.";
    c.run = [](sh::console::Context& ctx, const sh::console::Args& args) -> Result<void> {
        State& s = st();
        const std::string& a = args.str(0);
        std::istringstream in(args.joined(1));
        std::vector<std::string> w;
        for (std::string t; in >> t;) w.push_back(t);
        if (a == "open") return report(ctx, open_mod(w.empty() ? "" : w[0]));
        if (s.mod.empty()) return Result<void>::fail("modobj: make (modnew) or open a mod first");
        if (s.map != map_now()) {
            if (s.dirty) (void)save();
            load_objects();
        }
        auto selected = [&]() -> dt::ModObject* {
            return s.selected >= 0 && s.selected < static_cast<int>(s.objects.size())
                       ? &s.objects[static_cast<std::size_t>(s.selected)]
                       : nullptr;
        };
        if (a == "add") {
            dt::ModObjectKind kind;
            if (w.empty() || !dt::parse_mod_object_kind(w[0], &kind))
                return Result<void>::fail("modobj add: message, script, spawn or pickup");
            Vec3 at = player_metres();
            std::int32_t yaw = gs::chara_rotation(0).y & 4095;
            std::size_t next = 2;
            const std::string where = w.size() > 1 ? w[1] : "player";
            if (where == "camera") {
                double cy = 0, cp = 0;
                camera_angles(&cy, &cp);
                at = camera_position(), yaw = yaw_q12(cy);
            } else if (where == "pick") {
                if (!last_pick().hit) return Result<void>::fail("modobj add: no world pick yet");
                at = last_pick().point;
            } else if (where != "player") {
                if (w.size() < 4) return Result<void>::fail("modobj add <kind> <x> <y> <z> [id] (metres)");
                at = {std::strtod(w[1].c_str(), nullptr), std::strtod(w[2].c_str(), nullptr),
                      std::strtod(w[3].c_str(), nullptr)};
                next = 4;
            }
            return report(ctx, add_object(kind, at, yaw, w.size() > next ? w[next] : ""));
        }
        if (a == "select") {
            for (int i = 0; i < static_cast<int>(s.objects.size()); ++i) {
                if (!w.empty() && s.objects[static_cast<std::size_t>(i)].id == w[0]) {
                    s.selected = i;
                    s.text_for = -1;
                    set_status("selected " + w[0], false);
                    return report(ctx, Result<void>::ok());
                }
            }
            return Result<void>::fail("modobj select: no object " + (w.empty() ? "" : w[0]));
        }
        if (a == "list") {
            std::string line = s.mod + " on " + s.map + ":";
            for (const dt::ModObject& o : s.objects) {
                char buf[160];
                std::snprintf(buf, sizeof buf, " %s(%s %.2f %.2f %.2f)", o.id.c_str(), dt::mod_object_kind_name(o.kind),
                              metres(o.x), metres(o.y), metres(o.z));
                line += buf;
            }
            ctx.out.info("modobj: " + line + (s.dirty ? " (unsaved)" : ""));
            return Result<void>::ok();
        }
        if (a == "save") return report(ctx, save());
        if (a == "reload") {
            reload();
            return report(ctx, Result<void>::ok());
        }
        dt::ModObject* o = selected();
        if (o == nullptr) return Result<void>::fail("modobj " + a + ": no object selected");
        if (a == "move") {
            if (w.size() < 3) return Result<void>::fail("modobj move <dx> <dy> <dz> (metres)");
            for (int i = 0; i < 3; ++i) nudge(i, std::strtod(w[static_cast<std::size_t>(i)].c_str(), nullptr));
            set_status("moved " + o->id, false);
            return report(ctx, Result<void>::ok());
        }
        if (a == "set") {
            if (w.empty()) return Result<void>::fail("modobj set <field> <value>");
            const std::string all = args.joined(1);
            const std::size_t sp = all.find(' ');
            const std::string value = sp == std::string::npos ? "" : all.substr(sp + 1);
            if (auto r = set_field(*o, w[0], value); !r) return r;
            s.dirty = true;
            s.text_for = -1;
            set_status("set " + o->id + "." + w[0], false);
            return report(ctx, Result<void>::ok());
        }
        if (a == "del") {
            s.objects.erase(s.objects.begin() + s.selected);
            s.selected = std::min(s.selected, static_cast<int>(s.objects.size()) - 1);
            s.dirty = true;
            set_status("deleted", false);
            return report(ctx, Result<void>::ok());
        }
        if (a == "script") {
            if (s.dirty)
                if (auto r = save(); !r) return r;
            return report(ctx, create_script(*o));
        }
        return Result<void>::fail("modobj: unknown action " + a);
    };
    (void)reg.add(std::move(c));
}

struct Install {
    Install() {
        Panel p;
        p.id = "tools.mod";
        p.title = "Mod";
        p.menu = "Data";
        p.draw = &draw_panel;
        p.open = false;
        p.auto_size = false;
        register_panel(std::move(p));
        register_overlay({"tools.mod_objects", "Mod objects", &draw_overlay, true});
        register_selection_handler("tools.mod_place", 100, &on_pick);
        register_commands();
    }
};

const Install s_install;

}  // namespace

}  // namespace sh::tools
