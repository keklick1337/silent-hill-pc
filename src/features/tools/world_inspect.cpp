// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The editor's world inspector: what a left click on the world hit, among the
// characters of the map, the library models placed this frame (world objects, the held item) and the map's cells,
// with its model name, glb file (host file and owning mod) and its materials with texture thumbnails. The material
// of the triangle under the click is marked. Read only: it looks at the scene store, never at the game's state
// beyond the characters' positions.

#include "features/tools/asset_views.h"
#include "features/tools/tools.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "engine/assets/skin_model.h"
#include "engine/chara/chara_library.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/console/instance.h"
#include "engine/console/registry.h"
#include "engine/game_state/game_state.h"
#include "render/scene/area_cells.h"
#include "render/scene/scene.h"

namespace sh::tools::assets {

namespace {

namespace gs = sh::game_state;

constexpr double kUnitsPerMetre = 256.0;
constexpr double kCellUnits = 40.0 * kUnitsPerMetre;

struct V3 {
    double x = 0, y = 0, z = 0;
};
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// Closest point of triangle abc to p (barycentric regions).
V3 closest_on_triangle(V3 p, V3 a, V3 b, V3 c) {
    const V3 ab = b - a, ac = c - a, ap = p - a;
    const double d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return a;
    const V3 bp = p - b;
    const double d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return b;
    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
    const V3 cp = p - c;
    const double d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return c;
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    const double denom = 1.0 / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
}

// The material of the mesh triangle nearest to `p` (mesh space) and its distance; -1 for an empty mesh.
int nearest_material(const scene::CellMesh& mesh, V3 p, double* distance) {
    int best = -1;
    double best_d2 = std::numeric_limits<double>::max();
    auto vtx = [&mesh](std::uint32_t i) {
        const scene::CellVertex& v = mesh.vertices[i];
        return V3{v.x, v.y, v.z};
    };
    for (const scene::CellRange& r : mesh.ranges) {
        for (std::uint32_t i = r.first; i + 2 < r.first + r.count && i + 2 < mesh.indices.size(); i += 3) {
            const V3 q = closest_on_triangle(p, vtx(mesh.indices[i]), vtx(mesh.indices[i + 1]), vtx(mesh.indices[i + 2]));
            const V3 d = q - p;
            const double d2 = dot(d, d);
            if (d2 < best_d2) {
                best_d2 = d2;
                best = r.material;
            }
        }
    }
    if (distance) *distance = std::sqrt(best_d2);
    return best;
}

enum class Kind { None, Character, Object, Cell };

struct Hit {
    Kind kind = Kind::None;
    std::string name;    // character folder, model name, cell stem
    std::string file;    // gamedata path of the glb
    int slot = -1;       // character slot
    int material = -1;   // hit material
    double distance = 0; // metres from the click point to the hit triangle (objects, cells)
    std::shared_ptr<const scene::CellMesh> mesh;  // objects and cells
};

struct Inspector {
    Hit hit;
    bool have_pick = false;
};

Inspector& inspector() {
    static Inspector s;
    return s;
}

// The library model whose mesh this is, by the store's aliasing pointer; empty when the store has none.
scene::LoadedModel model_of(const scene::CellMesh* mesh) {
    for (const scene::LoadedModel& m : scene::loaded_models())
        if (&m.model->mesh == mesh) return m;
    return {};
}

// A character within half a metre of the camera ray, nearer than the surface hit: the posed meshes are not in the
// surface query, so the click is matched with each character's standing column.
Hit pick_character(const Pick& pick) {
    Hit best;
    double best_t = pick.hit ? dot({pick.point.x - pick.ray_origin.x, pick.point.y - pick.ray_origin.y,
                                    pick.point.z - pick.ray_origin.z},
                                   {pick.ray_dir.x, pick.ray_dir.y, pick.ray_dir.z}) + 0.3
                             : 400.0;
    const sh::chara::CharaLibrary* lib = Pc_CharaData_Library();
    for (int slot = 0; slot <= gs::kNpcSlots; ++slot) {
        const int id = gs::chara_id(slot);
        if (id <= 0) continue;
        const sh::chara::CharacterDef* def = lib != nullptr ? lib->find(id) : nullptr;
        if (def == nullptr) continue;
        const sh::entities::Vec3 q = gs::chara_position(slot);
        const V3 feet{metres(q.x), metres(q.y), metres(q.z)};
        const V3 o{pick.ray_origin.x, pick.ray_origin.y, pick.ray_origin.z};
        const V3 d{pick.ray_dir.x, pick.ray_dir.y, pick.ray_dir.z};
        // The ray's nearest approach to the vertical segment feet .. feet - 1.8 m (y is down).
        for (double h = 0.1; h <= 1.8; h += 0.1) {
            const V3 c{feet.x, feet.y - h, feet.z};
            const double t = dot(c - o, d);
            if (t <= 0 || t >= best_t) continue;
            const V3 off = o + d * t - c;
            if (std::hypot(off.x, off.z) > 0.45 || std::abs(off.y) > 0.1) continue;
            best_t = t;
            best.kind = Kind::Character;
            best.name = def->folder;
            best.file = def->model.empty() ? std::string() : "convert/" + def->model;
            best.slot = slot;
        }
    }
    return best;
}

Hit pick_object(const V3& p) {
    Hit best;
    double best_d = 4.0;  // PS1 units off the surface the click found
    for (const scene::ObjectRef& o : scene::placed_objects()) {
        if (!o.mesh) continue;
        // Model space: R^-1 (p - t), R row-major with one uniform scale.
        const V3 d{p.x - o.trans[0], p.y - o.trans[1], p.z - o.trans[2]};
        const double s2 = o.rot[0] * o.rot[0] + o.rot[3] * o.rot[3] + o.rot[6] * o.rot[6];
        if (s2 <= 1e-12) continue;
        const V3 q{(o.rot[0] * d.x + o.rot[3] * d.y + o.rot[6] * d.z) / s2,
                   (o.rot[1] * d.x + o.rot[4] * d.y + o.rot[7] * d.z) / s2,
                   (o.rot[2] * d.x + o.rot[5] * d.y + o.rot[8] * d.z) / s2};
        double dist = 0;
        const int m = nearest_material(*o.mesh, q, &dist);
        dist *= std::sqrt(s2);
        if (m < 0 || dist >= best_d) continue;
        best_d = dist;
        const scene::LoadedModel lm = model_of(o.mesh.get());
        best.kind = Kind::Object;
        best.name = lm.model ? lm.model->name : o.mesh->stem;
        best.file = lm.file;
        best.material = m;
        best.distance = dist / kUnitsPerMetre;
        best.mesh = o.mesh;
    }
    return best;
}

Hit pick_cell(const V3& p) {
    Hit best;
    double best_d = 8.0;
    for (const scene::CellRef& ref : scene::placed_cells()) {
        const V3 corner{ref.cell_x * kCellUnits, 0, ref.cell_z * kCellUnits};
        const V3 q = p - corner;
        if (q.x < -4 || q.z < -4 || q.x > kCellUnits + 4 || q.z > kCellUnits + 4) continue;
        const std::shared_ptr<const scene::CellMesh> mesh = scene::find_cell(ref.stem);
        if (!mesh) continue;
        double dist = 0;
        const int m = nearest_material(*mesh, q, &dist);
        if (m < 0 || dist >= best_d) continue;
        best_d = dist;
        best.kind = Kind::Cell;
        best.name = ref.stem;
        best.file = "maps/areas/" + ref.stem.substr(0, ref.stem.size() > 4 ? ref.stem.size() - 4 : 0) + ".glb";
        best.material = m;
        best.distance = dist / kUnitsPerMetre;
        best.mesh = mesh;
    }
    return best;
}

void inspect(const Pick& pick) {
    Inspector& in = inspector();
    in.have_pick = true;
    Hit hit = pick_character(pick);
    if (hit.kind == Kind::None && pick.hit) {
        const V3 p{pick.point.x * kUnitsPerMetre, pick.point.y * kUnitsPerMetre, pick.point.z * kUnitsPerMetre};
        hit = pick_object(p);
        if (hit.kind == Kind::None) hit = pick_cell(p);
    }
    in.hit = std::move(hit);
}

const char* blend_name(scene::Blend b) {
    switch (b) {
        case scene::Blend::Opaque: return "opaque";
        case scene::Blend::Additive: return "additive";
        case scene::Blend::Average: return "alpha";
        case scene::Blend::Mean: return "half + half";
        case scene::Blend::Subtractive: return "subtractive";
        case scene::Blend::AddQuarter: return "add quarter";
    }
    return "?";
}

// A host file below the gamedata folder or a mod's as the path the data names it by.
std::string shown_path(const std::string& host) {
    for (const char* mark : {"/gamedata/", "/mods/"}) {
        const std::size_t at = host.rfind(mark);
        if (at != std::string::npos) return host.substr(at + 1);
    }
    return host;
}

void file_lines(const std::string& file) {
    if (file.empty()) return;
    ImGui::Text("file  %s", file.c_str());
    const std::string host = host_file(file);
    const std::string owner = owner_mod(file);
    ImGui::TextDisabled("  %s", host.empty() ? "(no file)" : host.c_str());
    ImGui::TextDisabled("  from %s", owner.empty() ? "base gamedata" : ("mod " + owner).c_str());
}

void draw_mesh_materials(const Hit& hit) {
    const scene::CellMesh& mesh = *hit.mesh;
    ImGui::Text("%zu vertices, %zu triangles, %zu materials", mesh.vertices.size(), mesh.indices.size() / 3,
                mesh.materials.size());
    const float thumb = ImGui::GetTextLineHeight() * 3.0f;
    if (!ImGui::BeginTable("materials", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) return;
    ImGui::TableSetupColumn("tex", ImGuiTableColumnFlags_WidthFixed, thumb + 4);
    ImGui::TableSetupColumn("material", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("go", ImGuiTableColumnFlags_WidthFixed);
    for (std::size_t i = 0; i < mesh.materials.size(); ++i) {
        const scene::CellMaterial& m = mesh.materials[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();
        if (static_cast<int>(i) == hit.material)
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(120, 90, 20, 160));
        ImGui::TableNextColumn();
        // Only what the store already holds: no file is read from the draw.
        const Picture* pic = nullptr;
        if (!m.texture.empty())
            for (const scene::LoadedTexture& t : scene::loaded_textures())
                if (t.path == m.texture && t.palette == m.palette) pic = scene_picture(t.texture);
        if (pic) image_on_checker(*pic, ImVec2(thumb, thumb));
        else ImGui::Dummy(ImVec2(thumb, thumb));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(m.texture.empty() ? (m.unpictured.empty() ? "(untextured)" : m.unpictured.c_str())
                                                 : m.texture.c_str());
        ImGui::TextDisabled("palette %d   %s%s%s", m.palette, m.semi_transparent ? "semi-transparent, " : "",
                            m.semi_transparent ? blend_name(m.blend) : "solid",
                            static_cast<int>(i) == hit.material ? "   <- clicked" : "");
        ImGui::TableNextColumn();
        if (!m.texture.empty() && ImGui::SmallButton("Browse")) browse_texture(m.texture, m.palette);
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void draw_character(const Hit& hit) {
    std::string error;
    const sh::assets::SkinModel* model = character_model(hit.name, &error);
    ImGui::Text("model  %s", hit.file.c_str());
    const std::string host = character_model_file(hit.name);
    const std::string owner = owner_mod(hit.file);
    ImGui::TextDisabled("  %s", host.empty() ? "(no file)" : host.c_str());
    ImGui::TextDisabled("  from %s", owner.empty() ? "base gamedata" : ("mod " + owner).c_str());
    if (model == nullptr) {
        ImGui::TextDisabled("%s", error.empty() ? "reading the model..." : error.c_str());
        return;
    }
    std::size_t triangles = 0;
    for (const auto& g : model->groups) triangles += g.indices.size() / 3;
    ImGui::Text("%zu vertices, %zu triangles, %d bones, %zu materials", model->vertices.size(), triangles,
                model->bone_count, model->materials.size());
    const float thumb = ImGui::GetTextLineHeight() * 3.0f;
    if (!ImGui::BeginTable("materials", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) return;
    ImGui::TableSetupColumn("tex", ImGuiTableColumnFlags_WidthFixed, thumb + 4);
    ImGui::TableSetupColumn("material", ImGuiTableColumnFlags_WidthStretch);
    for (std::size_t i = 0; i < model->materials.size(); ++i) {
        const sh::assets::SkinMaterial& m = model->materials[i];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (const Picture* pic = character_texture(hit.name, m.texture)) image_on_checker(*pic, ImVec2(thumb, thumb));
        else ImGui::Dummy(ImVec2(thumb, thumb));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(m.name.empty() ? "(material)" : m.name.c_str());
        std::string tex = "(untextured)";
        if (m.texture >= 0 && static_cast<std::size_t>(m.texture) < model->textures.size()) {
            const sh::assets::SkinTexture& t = model->textures[static_cast<std::size_t>(m.texture)];
            tex = t.file.empty() ? "(stored in the glb)" : shown_path(t.file.generic_string());
            tex += "  " + std::to_string(t.width) + "x" + std::to_string(t.height);
        }
        ImGui::TextDisabled("%s", tex.c_str());
        ImGui::TextDisabled("%s%s", m.alpha == sh::assets::SkinAlphaMode::Blend ? "blend" :
                                    m.alpha == sh::assets::SkinAlphaMode::Mask ? "mask" : "opaque",
                            m.semi_transparent ? ", semi-transparent (STP)" : "");
    }
    ImGui::EndTable();
}

void draw_inspector() {
    static bool s_placed = false;
    if (!s_placed) {
        // A first size and place, once: the host opens a user-sized panel with no height.
        s_placed = true;
        const ImVec2 screen = ImGui::GetMainViewport()->Size;
        const float scale = std::max(1.0f, screen.y / 720.0f);
        ImGui::SetWindowSize(ImVec2(std::min(520.0f * scale, screen.x * 0.68f), screen.y * 0.9f), ImGuiCond_Always);
        ImGui::SetWindowPos(ImVec2(screen.x - std::min(520.0f * scale, screen.x * 0.68f) - 8.0f, screen.y * 0.05f),
                            ImGuiCond_Always);
    }
    Inspector& in = inspector();
    if (!in.have_pick) {
        ImGui::TextWrapped("Left-click a character, a world object or the map to see its model, file and materials.");
        return;
    }
    const Hit& hit = in.hit;
    switch (hit.kind) {
        case Kind::None:
            ImGui::TextDisabled("Nothing of the scene under the click.");
            return;
        case Kind::Character: {
            ImGui::Text("Character  %s  (slot %d)", hit.name.c_str(), hit.slot);
            if (ImGui::Button("Open in model viewer")) view_character(hit.name);
            draw_character(hit);
            return;
        }
        case Kind::Object:
            ImGui::Text("World object  %s", hit.name.c_str());
            ImGui::TextDisabled("%.3f m from the click", hit.distance);
            if (!hit.file.empty() && ImGui::Button("Open in model viewer")) view_library_model(hit.file, hit.name);
            break;
        case Kind::Cell:
            ImGui::Text("Map cell  %s", hit.name.c_str());
            ImGui::TextDisabled("%.3f m from the click", hit.distance);
            break;
    }
    file_lines(hit.file);
    if (hit.mesh) draw_mesh_materials(hit);
}

// For scripted runs: open the hit's texture in the browser or its model in the viewer.
void register_inspect_command() {
    using sh::console::ArgSpec;
    using sh::console::ArgType;
    sh::console::CommandSpec cmd;
    cmd.name = "editorinspect";
    cmd.group = "debug";
    cmd.help = "act on the world inspector's last hit: browse (its clicked material's texture) or model (the viewer)";
    cmd.args = {ArgSpec{"what", ArgType::Enum, false, "browse or model", {"browse", "model"}, {}}};
    cmd.run = [](sh::console::Context& ctx, const sh::console::Args& args) -> Result<void> {
        const Hit& hit = inspector().hit;
        if (args.str(0) == "model") {
            if (hit.kind == Kind::Character) view_character(hit.name);
            else if (hit.kind == Kind::Object && !hit.file.empty()) view_library_model(hit.file, hit.name);
            else ctx.out.error("editorinspect: the hit is no character or model");
        } else if (hit.mesh && hit.material >= 0 && static_cast<std::size_t>(hit.material) < hit.mesh->materials.size()) {
            const scene::CellMaterial& m = hit.mesh->materials[static_cast<std::size_t>(hit.material)];
            if (!m.texture.empty()) browse_texture(m.texture, m.palette);
        } else {
            ctx.out.error("editorinspect: no material was hit");
        }
        return Result<void>::ok();
    };
    (void)sh::console::global_registry().add(std::move(cmd));
}

struct Install {
    Install() {
        register_inspect_command();
        Panel p;
        p.id = "tools.inspector";
        p.title = "World inspector";
        p.menu = "Assets";
        p.draw = &draw_inspector;
        p.open = false;
        p.auto_size = false;
        register_panel(std::move(p));
        // Before the coordinates (1000) and never taking the click: both panels follow the same pick.
        register_selection_handler("tools.inspector", 500, [](const Pick& pick) {
            if (pick.button == 0) inspect(pick);
            return false;
        });
    }
};

const Install s_install;

}  // namespace

}  // namespace sh::tools::assets
