// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The editor's built-in panels, registered through tools.h like any later tool:
// the viewer options, the navigation (maps, warps, positions) and the coordinates of the picked world point with
// the Lua and JSON forms to copy. Also the overlays they own: the cell bounds and the player marker.

#include "features/tools/tools.h"
#include "features/tools/tools_internal.h"

#include <imgui.h>

#include <cmath>
#include <numbers>
#include <cstdio>
#include <cstring>
#include <string>

#include "engine/devtools/position_snippets.h"
#include "engine/game_state/game_state.h"
#include "render/scene/scene.h"

namespace sh::tools {

namespace {

namespace gs = sh::game_state;
namespace dt = sh::devtools;

constexpr double kCellMetres = 40.0;

Vec3 player_position() {
    const sh::entities::Vec3 p = gs::chara_position(0);
    return {metres(p.x), metres(p.y), metres(p.z)};
}

void copy_button(const char* id, const std::string& text) {
    ImGui::PushID(id);
    if (ImGui::SmallButton("Copy")) copy_to_clipboard(text);
    ImGui::PopID();
}

// --- viewer ---

void draw_viewer() {
    ViewerSettings& v = viewer();
    ImGui::SeparatorText("Fog and distance");
    ImGui::Checkbox("Fog off (whole room / map visible)", &v.fog_off);
    ImGui::SeparatorText("Lighting");
    ImGui::Checkbox("Full bright", &v.full_bright);
    ImGui::SliderFloat(v.full_bright ? "Light level" : "Map light scale", &v.light_scale, 0.0f, 3.0f, "%.2f");
    ImGui::Checkbox("Flashlight off", &v.flashlight_off);
    ImGui::SliderFloat("Brightness", &v.brightness, 0.25f, 3.0f, "%.2f");
    ImGui::SliderFloat("Contrast", &v.contrast, 0.25f, 2.0f, "%.2f");
    ImGui::SeparatorText("Debug views");
    ImGui::Checkbox("Wireframe", &v.wireframe);
    ImGui::Checkbox("Collision view (collvis)", &v.collision_view);
    bool cells = overlay_enabled("tools.cell_bounds");
    if (ImGui::Checkbox("Cell bounds (40 m grid)", &cells)) set_overlay_enabled("tools.cell_bounds", cells);
    bool marker = overlay_enabled("tools.player_marker");
    if (ImGui::Checkbox("Player marker", &marker)) set_overlay_enabled("tools.player_marker", marker);
    ImGui::Separator();
    if (ImGui::Button("Reset")) v = ViewerSettings{};
    ImGui::SameLine();
    if (ImGui::Button("Save viewer settings")) save_viewer_settings();
    ImGui::TextDisabled("Applied only in the editor; leaving restores the game's look.");
}

// --- navigation ---

char s_map_filter[64] = "";
float s_teleport[3] = {0.f, 0.f, 0.f};

void draw_navigation() {
    const int map = gs::current_map();
    const char* name = gs::map_name(map);
    const char* desc = gs::map_description(map);
    ImGui::Text("Map  %s  (room %d)", name ? name : "?", gs::save_room());
    if (desc) ImGui::TextWrapped("%s", desc);

    const Vec3 p = player_position();
    const sh::entities::Vec3 pq = gs::chara_position(0);
    const double yaw = gs::chara_rotation(0).y * 360.0 / 4096.0;
    ImGui::SeparatorText("Player");
    ImGui::Text("%.3f  %.3f  %.3f m   yaw %.1f", p.x, p.y, p.z, yaw);
    ImGui::TextDisabled("Q12 %d %d %d", pq.x, pq.y, pq.z);
    ImGui::Text("Cell %d, %d", static_cast<int>(std::floor(p.x / kCellMetres)),
                static_cast<int>(std::floor(p.z / kCellMetres)));

    ImGui::SeparatorText("Camera");
    const Vec3 c = camera_position();
    double cy = 0, cp = 0;
    camera_angles(&cy, &cp);
    ImGui::Text("%.3f  %.3f  %.3f m", c.x, c.y, c.z);
    ImGui::Text("yaw %.1f  pitch %.1f", cy, cp);
    if (ImGui::Button("Camera to player")) place_camera({p.x - 2.0 * std::sin(yaw * std::numbers::pi / 180.0), p.y - 1.7,
                                                        p.z - 2.0 * std::cos(yaw * std::numbers::pi / 180.0)}, yaw);
    ImGui::SameLine();
    if (ImGui::Button("Player to camera")) {
        const std::int32_t x = q12(c.x), z = q12(c.z);
        defer([x, z] { gs::place_player(x, z, nullptr); });
    }
    ImGui::InputFloat3("x y z (m)", s_teleport, "%.3f");
    if (ImGui::Button("Teleport camera")) place_camera({s_teleport[0], s_teleport[1], s_teleport[2]}, cy);
    ImGui::SameLine();
    if (ImGui::Button("Teleport player")) {
        const std::int32_t x = q12(s_teleport[0]), y = q12(s_teleport[1]), z = q12(s_teleport[2]);
        defer([x, y, z] { gs::place_player(x, z, &y); });
    }

    ImGui::SeparatorText("Maps");
    ImGui::InputTextWithHint("##filter", "filter (name or description)", s_map_filter, sizeof(s_map_filter));
    if (ImGui::BeginChild("maps", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 10), ImGuiChildFlags_Borders)) {
        for (int i = 0; i < gs::map_count(); ++i) {
            const char* n = gs::map_name(i);
            if (n == nullptr) continue;
            const char* d = gs::map_description(i);
            if (s_map_filter[0] != '\0' && std::strstr(n, s_map_filter) == nullptr &&
                (d == nullptr || std::strstr(d, s_map_filter) == nullptr))
                continue;
            ImGui::PushID(i);
            if (ImGui::SmallButton("Warp")) warp_to_map(n);
            ImGui::SameLine();
            ImGui::Text("%s%s", n, i == map ? "  (here)" : "");
            if (d && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d);
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::TextDisabled("A warp is a new game on that map; the editor opens again there.");
}

// --- coordinates ---

void draw_coordinates() {
    const Pick& pick = last_pick();
    if (!pick.hit) {
        ImGui::TextWrapped("Left-click the world (outside the panels) to pick a point on the drawn geometry.");
        return;
    }
    const dt::Q12Point q = dt::q12_from_geometry(pick.point.x * 256.0, pick.point.y * 256.0, pick.point.z * 256.0);
    ImGui::Text("Metres  %s  %s  %s", dt::metres_text(q.x).c_str(), dt::metres_text(q.y).c_str(),
                dt::metres_text(q.z).c_str());
    ImGui::Text("Q12     %d  %d  %d", q.x, q.y, q.z);
    ImGui::TextDisabled("normal %.2f %.2f %.2f   cell %d, %d", pick.normal.x, pick.normal.y, pick.normal.z,
                        static_cast<int>(std::floor(pick.point.x / kCellMetres)),
                        static_cast<int>(std::floor(pick.point.z / kCellMetres)));
    if (ImGui::Button("Place player here")) {
        const std::int32_t x = q.x, y = q.y, z = q.z;
        defer([x, y, z] { gs::place_player(x, z, &y); });
    }
    ImGui::SameLine();
    if (ImGui::Button("Camera here")) {
        double yaw = 0;
        camera_angles(&yaw, nullptr);
        place_camera({pick.point.x, pick.point.y - 1.7, pick.point.z}, yaw);
    }
    ImGui::SeparatorText("Copy as");
    const std::int32_t yaw = gs::chara_rotation(0).y;
    int i = 0;
    for (const dt::Snippet& s : dt::position_snippets(q, yaw)) {
        char id[16];
        std::snprintf(id, sizeof(id), "s%d", i++);
        copy_button(id, s.text);
        ImGui::SameLine();
        ImGui::TextUnformatted(s.label.c_str());
        ImGui::TextDisabled("  %s", s.text.c_str());
    }
}

// --- overlays ---

void draw_cell_bounds(Draw3D& d) {
    const double y = player_position().y;
    const Color line{0.3f, 0.8f, 1.0f, 0.9f};
    for (const scene::CellRef& c : scene::placed_cells()) {
        const double x0 = c.cell_x * kCellMetres, z0 = c.cell_z * kCellMetres;
        const double x1 = x0 + kCellMetres, z1 = z0 + kCellMetres;
        d.line({x0, y, z0}, {x1, y, z0}, line);
        d.line({x1, y, z0}, {x1, y, z1}, line);
        d.line({x1, y, z1}, {x0, y, z1}, line);
        d.line({x0, y, z1}, {x0, y, z0}, line);
        char text[64];
        std::snprintf(text, sizeof(text), "%s (%d, %d)", c.stem.c_str(), c.cell_x, c.cell_z);
        d.label({x0 + kCellMetres * 0.5, y, z0 + kCellMetres * 0.5}, text, line);
    }
}

void draw_player_marker(Draw3D& d) {
    const Vec3 p = player_position();
    const Color c{0.2f, 1.0f, 0.3f, 1.0f};
    d.circle_xz(p, 0.4, c, 24, 2.0f);
    d.line(p, {p.x, p.y - 1.8, p.z}, c, 2.0f);
    d.label({p.x, p.y - 1.9, p.z}, "player", c);
}

}  // namespace

void register_builtin_panels() {
    register_panel({"tools.viewer", "Viewer", "View", &draw_viewer, true});
    register_panel({"tools.navigation", "Navigation", "World", &draw_navigation, true});
    register_panel({"tools.coordinates", "Coordinates", "World", &draw_coordinates, true});
    register_overlay({"tools.cell_bounds", "Cell bounds (40 m grid)", &draw_cell_bounds, false});
    register_overlay({"tools.player_marker", "Player marker", &draw_player_marker, true});
}

}  // namespace sh::tools
