// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The modding tools' Placement panel (docs/modding/editor.md): a click on the world gives the exact point (metres,
// the cell, the room), a model can be tried there (a world model of the map, an item model file or a mod's glb),
// moved, turned about all three axes and scaled while it is drawn in the world, and the panel copies the entry in
// the forms a mod writes: a world_objects.json object, a pickup_create call, a Lua table row. It changes nothing in
// the game: the try-out is drawn by the world object runtime's tool hook, only while the editor is open.

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "engine/console/instance.h"
#include "engine/console/registry.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/game_state_world.h"
#include "engine/maps/map_runtime.h"
#include "engine/runtime/world_object_runtime.h"
#include "engine/world/object_queue.h"
#include "engine/world/world_objects.h"
#include "features/tools/tools.h"
#include "features/tools/tools_internal.h"

namespace sh::tools {

namespace {

namespace gs = sh::game_state;

constexpr const char* kPreviewName = "~PLACE";  // the try-out's model name in the item model registry
constexpr std::int32_t kNoGround = 8 * 4096;    // the ground query's answer where nothing is loaded

struct State {
    bool armed = false;  // the next left click on the world sets the point
    bool have_point = false;
    double pos[3] = {0, 0, 0};  // metres, y down
    float rot[3] = {0, 0, 0};   // degrees
    float scale = 1.0f;
    float step = 0.05f;    // metres per arrow key press
    char model[128] = "";  // a world model name (<= 8), "ITEM/<NAME>.TMD" or "<mod>/<name>.glb"
    bool preview = true;
    std::string map;
    gs::PickupRecord record = nullptr;  // the try-out's draw record (made again on another map)
    std::string record_model;
};
State s;

bool item_file(const std::string& m) {
    return m.find('/') != std::string::npos;  // a file below convert/models/; else a model name of the map
}

std::string fmt(double v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.3f", v);
    std::string t = b;
    while (t.size() > 1 && t.back() == '0' && t[t.size() - 2] != '.') t.pop_back();
    return t;
}

std::string object_snippet() {
    const std::string m = s.model[0] ? s.model : "MODEL";
    std::string out = "{\"name\": \"MyObject\", ";
    if (item_file(m)) {
        out += "\"model\": \"MYOBJ\", \"item_model\": {\"file\": \"" + m + "\", \"scale\": " + fmt(s.scale) + "}, ";
    } else {
        out += "\"model\": \"" + m + "\", ";
    }
    out += "\"position\": [" + fmt(s.pos[0]) + ", " + fmt(s.pos[1]) + ", " + fmt(s.pos[2]) + "], ";
    out += "\"rotation\": [" + fmt(s.rot[0]) + ", " + fmt(s.rot[1]) + ", " + fmt(s.rot[2]) + "]}";
    return out;
}

std::string pickup_snippet() {
    const int yaw = static_cast<int>(std::lround(s.rot[1] / 360.0 * 4096.0)) & 0xFFF;
    return "sh.world.pickup_create { item = \"HealthDrink\", x = " + fmt(s.pos[0]) + ", y = " + fmt(s.pos[1]) +
           ", z = " + fmt(s.pos[2]) + ", rot = " + std::to_string(yaw) +
           ", id = \"my_pickup\", flag = \"my_pickup_taken\" }";
}

std::string lua_row() { return "{ " + fmt(s.pos[0]) + ", " + fmt(s.pos[2]) + " },  -- y " + fmt(s.pos[1]); }

// The ground under the point, from the collision of the loaded cells (the point keeps its height without one).
void snap_to_ground() {
    defer([] {
        const std::int32_t y = gs::ground_height(q12(s.pos[0]), q12(s.pos[2]));
        if (y != kNoGround) s.pos[1] = metres(y);
    });
}

// The try-out, drawn after the map's world objects of the frame.
void draw_preview() {
    if (!active() || !s.preview || !s.have_point || s.model[0] == '\0') return;
    const std::string map = sh::maps::map_runtime().name();
    if (map != s.map || s.record == nullptr) {
        s.map = map;
        s.record = nullptr;
        s.record_model.clear();
    }
    const std::string m = s.model;
    const std::string name = item_file(m) ? kPreviewName : m.substr(0, 8);
    if (item_file(m)) sh::world::object_item_model_set(kPreviewName, {m, std::max(0.01f, s.scale)});
    if (s.record == nullptr || s.record_model != name) {
        s.record = gs::pickup_record_create(name.c_str());
        s.record_model = name;
    }
    gs::pickup_record_set_pose(
        s.record, {q12(s.pos[0]), q12(s.pos[1]), q12(s.pos[2])},
        {sh::world::world_degrees_to_angle(s.rot[0]), sh::world::world_degrees_to_angle(s.rot[1]),
         sh::world::world_degrees_to_angle(s.rot[2])});
    gs::pickup_record_draw(s.record);
}

bool on_pick(const Pick& pick) {
    if (!s.armed || pick.button != 0 || !pick.hit) return false;
    s.pos[0] = pick.point.x;
    s.pos[1] = pick.point.y;
    s.pos[2] = pick.point.z;
    s.have_point = true;
    return true;
}

void nudge_keys() {
    if (!s.have_point || ImGui::GetIO().WantTextInput) return;
    const bool shift = ImGui::GetIO().KeyShift;
    const double d = s.step;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) s.pos[0] -= d;
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) s.pos[0] += d;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) (shift ? s.pos[1] : s.pos[2]) += shift ? -d : d;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) (shift ? s.pos[1] : s.pos[2]) -= shift ? -d : d;
}

void draw_panel() {
    ImGui::Checkbox("click sets the point", &s.armed);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The next left clicks on the world put the point where they hit.");
    ImGui::SameLine();
    if (ImGui::SmallButton("at player")) {
        const sh::entities::Vec3 p = gs::chara_position(0);
        s.pos[0] = metres(p.x);
        s.pos[1] = metres(p.y);
        s.pos[2] = metres(p.z);
        s.have_point = true;
    }
    if (!s.have_point) {
        ImGui::TextDisabled("Click the world (with the box ticked) or take the player's place.");
        return;
    }
    nudge_keys();
    ImGui::SeparatorText("Point (metres, y grows down)");
    ImGui::DragScalarN("x y z", ImGuiDataType_Double, s.pos, 3, 0.01f, nullptr, nullptr, "%.3f");
    ImGui::SliderFloat("arrow step (m)", &s.step, 0.005f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    if (ImGui::SmallButton("snap to ground")) snap_to_ground();
    ImGui::SameLine();
    if (ImGui::SmallButton("player here")) {
        const double x = s.pos[0], z = s.pos[2];
        run_command("tp " + fmt(x) + " " + fmt(z));
    }
    const int cx = static_cast<int>(std::floor(s.pos[0] / 40.0)), cz = static_cast<int>(std::floor(s.pos[2] / 40.0));
    ImGui::Text("cell x %d, z %d  (zone {\"x\": %d, \"z\": %d});  the player's room %d", cx, cz, cx, cz,
                gs::save_room());
    ImGui::TextDisabled("Arrows move along x and z, Shift+Up/Down up and down.");

    ImGui::SeparatorText("Try a model here");
    ImGui::InputTextWithHint("model", "BULLET_N, ITEM/UNQ41.TMD, mymod/prop.glb", s.model, sizeof(s.model));
    ImGui::Checkbox("draw it", &s.preview);
    ImGui::DragFloat3("rotation (deg)", s.rot, 0.5f, -360.0f, 360.0f, "%.1f");
    for (int i = 0; i < 3; ++i) {
        ImGui::PushID(i);
        if (i) ImGui::SameLine();
        static const char* kAxes[] = {"x", "y", "z"};
        if (ImGui::SmallButton((std::string(kAxes[i]) + " -90").c_str())) s.rot[i] -= 90.0f;
        ImGui::SameLine();
        if (ImGui::SmallButton((std::string(kAxes[i]) + " +90").c_str())) s.rot[i] += 90.0f;
        ImGui::PopID();
    }
    if (item_file(s.model)) ImGui::DragFloat("scale", &s.scale, 0.01f, 0.01f, 20.0f, "%.3f");

    ImGui::SeparatorText("Copy");
    if (ImGui::Button("world_objects.json")) copy_to_clipboard(object_snippet());
    ImGui::SameLine();
    if (ImGui::Button("pickup_create")) copy_to_clipboard(pickup_snippet());
    ImGui::SameLine();
    if (ImGui::Button("Lua { x, z }")) copy_to_clipboard(lua_row());
    ImGui::TextWrapped("%s", object_snippet().c_str());
}

// `placement <x> <y> <z> [model] [rx] [ry] [rz] [scale]`: the panel's point and try-out for scripted runs.
void register_command() {
    using sh::console::ArgSpec;
    using sh::console::ArgType;
    sh::console::CommandSpec spec;
    spec.name = "placement";
    spec.group = "debug";
    spec.help = "set the Placement panel's point (metres, y down) and the model it tries there";
    spec.args = {ArgSpec{"x", ArgType::Float, false, "metres", {}, {}},
                 ArgSpec{"y", ArgType::Float, false, "metres, +down", {}, {}},
                 ArgSpec{"z", ArgType::Float, false, "metres", {}, {}},
                 ArgSpec{"model", ArgType::String, true, "model", {}, {}},
                 ArgSpec{"rx", ArgType::Float, true, "degrees", {}, {}},
                 ArgSpec{"ry", ArgType::Float, true, "degrees", {}, {}},
                 ArgSpec{"rz", ArgType::Float, true, "degrees", {}, {}},
                 ArgSpec{"scale", ArgType::Float, true, "file models", {}, {}}};
    spec.run = [](sh::console::Context& ctx, const sh::console::Args& args) -> Result<void> {
        s.pos[0] = args.number(0);
        s.pos[1] = args.number(1);
        s.pos[2] = args.number(2);
        s.have_point = true;
        if (args.has(3)) std::snprintf(s.model, sizeof(s.model), "%s", args.str(3).c_str());
        for (int i = 0; i < 3; ++i) s.rot[i] = static_cast<float>(args.number(4 + i, 0.0));
        s.scale = static_cast<float>(args.number(7, 1.0));
        ctx.out.info(object_snippet());
        return Result<void>::ok();
    };
    (void)sh::console::global_registry().add(std::move(spec));
}

struct Install {
    Install() {
        register_command();
        register_panel({"tools.placement", "Placement", "World", &draw_panel, false, true});
        register_selection_handler("tools.placement", 50, &on_pick);
        sh::world::WorldObjectRuntime::set_tool_draw(&draw_preview);
    }
};

const Install s_install;

}  // namespace

}  // namespace sh::tools
