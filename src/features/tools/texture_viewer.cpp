// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The editor's texture browser: the textures the scene has read and every
// picture under convert/textures of gamedata and the active mods, with a filter; the picked one zoomed over a
// checkerboard (holes and semi-transparent texels show), its palette variants, size, format, host file and owner, the
// cells, models and characters that use it, and the world highlight of every surface drawn with it.

#include "features/tools/asset_views.h"
#include "features/tools/tools.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "engine/chara/chara_library.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/console/instance.h"
#include "engine/console/registry.h"
#include "engine/mods/registry.h"
#include "engine/vfs/vfs_c.h"
#include "formats/common/json.h"
#include "formats/png/palette_variant.h"
#include "render/scene/area_cells.h"
#include "render/scene/scene.h"

namespace sh::tools::assets {

namespace {

namespace fs = std::filesystem;

constexpr const char* kTextureRoot = "convert/textures/";

struct FileEntry {
    std::string path;  // gamedata path
    std::string mod;   // the mod that has it ("" base)
};

struct Selected {
    std::string path;
    std::string material;  // the path materials name it by (the highlight's key); path may be a palette variant of it
    int palette = -1;
    std::shared_ptr<const scene::SceneTexture> texture;
    bool loaded = false;  // the read was done (texture may still be null)
    // From the TIM description next to it.
    int bpp = 0;
    int palettes = 0;
    std::string game_path;
    std::uint32_t epoch = 0;
};

struct Browser {
    int source = 0;  // 0 the scene's textures, 1 the files
    char filter[96] = "";
    std::vector<FileEntry> files;
    bool files_scanned = false;
    Selected sel;
    float zoom = 2.0f;
    bool highlight = false;
    int drawn_frame = -10;  // ImGui frame the browser was last drawn in
};

Browser& browser() {
    static Browser* b = new Browser;  // pictures live in the overlay; never torn down at exit
    return *b;
}

bool matches(const std::string& text, const char* filter) {
    if (filter[0] == '\0') return true;
    std::string a = text, b = filter;
    std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return a.find(b) != std::string::npos;
}

// The palette a texture path names: "X.p09.png" -> 9; -1 without one.
int palette_of(const std::string& path) {
    const auto v = sh::png::parse_variant_name(path);
    return v ? v->row : -1;
}

// "convert/textures/BG/X.p09.png" -> "convert/textures/BG/X.png".
std::string base_of(const std::string& path) {
    const auto v = sh::png::parse_variant_name(path);
    return v ? v->base_png : path;
}

std::string variant_path(const std::string& path, int row) {
    const std::string base = base_of(path);
    char suffix[16];
    std::snprintf(suffix, sizeof(suffix), ".p%02d.png", row);
    return base.substr(0, base.size() - 4) + suffix;
}

// Reads the picture and its TIM description: at the next update, never from the draw.
void load_selected() {
    Browser& b = browser();
    const std::string path = b.sel.path;
    const int palette = b.sel.palette;
    defer([path, palette] {
        Browser& br = browser();
        if (br.sel.path != path || br.sel.palette != palette) return;
        br.sel.texture = scene::find_texture(path, palette);
        br.sel.loaded = true;
        br.sel.epoch = asset_epoch();
        br.sel.bpp = 0;
        br.sel.palettes = 0;
        br.sel.game_path.clear();
        if (auto tim = scene::read_scene_file(scene::tim_json_path(base_of(path)))) {
            auto doc = sh::json::parse(std::string_view(reinterpret_cast<const char*>(tim->data()), tim->size()));
            if (doc && doc.value().is_object()) {
                const sh::json::Value& v = doc.value();
                if (const auto* bpp = v.find("bpp"); bpp && bpp->is_number()) br.sel.bpp = static_cast<int>(bpp->number());
                if (const auto* p = v.find("palettes"); p && p->is_array())
                    br.sel.palettes = static_cast<int>(p->array().size());
                if (const auto* g = v.find("game_path"); g && g->is_string()) br.sel.game_path = g->string();
            }
        }
    });
}

void select(const std::string& path, int palette, const std::string& material = {}) {
    Browser& b = browser();
    if (b.sel.path == path && b.sel.palette == palette && b.sel.loaded && b.sel.epoch == asset_epoch()) return;
    b.sel = Selected{};
    b.sel.path = path;
    b.sel.material = material.empty() ? path : material;
    b.sel.palette = palette;
    load_selected();
}

void scan_files() {
    defer([] {
        Browser& b = browser();
        b.files.clear();
        std::set<std::string> seen;
        // Mods first: the copy that is read wins the row.
        sh::mods::Registry& reg = sh::mods::registry();
        if (reg.configured()) {
            const auto active = reg.active();
            for (auto it = active.rbegin(); it != active.rend(); ++it) {
                for (const sh::mods::ModFile& f : reg.files((*it)->candidate.manifest.id)) {
                    if (f.path.rfind(kTextureRoot, 0) != 0 || f.path.size() < 4 ||
                        f.path.compare(f.path.size() - 4, 4, ".png") != 0)
                        continue;
                    if (seen.insert(f.path).second) b.files.push_back({f.path, (*it)->candidate.manifest.id});
                }
            }
        }
        const char* root = ShVfs_GameDataDir();
        if (root != nullptr && root[0] != '\0') {
            const fs::path dir = fs::path(root) / "convert" / "textures";
            std::error_code ec;
            for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
                if (!it->is_regular_file(ec) || it->path().extension() != ".png") continue;
                const std::string rel = std::string(kTextureRoot) + fs::relative(it->path(), dir, ec).generic_string();
                if (seen.insert(rel).second) b.files.push_back({rel, {}});
            }
        }
        std::sort(b.files.begin(), b.files.end(), [](const FileEntry& x, const FileEntry& y) { return x.path < y.path; });
        b.files_scanned = true;
    });
}

// Who draws `path`: cells and library models of the store by material, characters by their texture list.
struct Users {
    std::vector<std::string> cells, models, characters;
};

Users users_of(const std::string& path) {
    Users u;
    for (const auto& cell : scene::loaded_cells()) {
        for (const scene::CellMaterial& m : cell->materials) {
            if (m.texture != path) continue;
            u.cells.push_back(cell->stem);
            break;
        }
    }
    for (const scene::LoadedModel& lm : scene::loaded_models()) {
        for (const scene::CellMaterial& m : lm.model->mesh.materials) {
            if (m.texture != path) continue;
            u.models.push_back(lm.model->name + "  (" + lm.file + ")");
            break;
        }
    }
    // Character pictures are named below convert/ ("textures/CHARA/HERO.png").
    const std::string below = path.rfind("convert/", 0) == 0 ? path.substr(8) : path;
    const std::string base = base_of(below);
    if (const sh::chara::CharaLibrary* lib = Pc_CharaData_Library()) {
        for (const sh::chara::CharacterDef& def : lib->characters()) {
            if (std::find(def.textures.begin(), def.textures.end(), base) != def.textures.end())
                u.characters.push_back(def.folder);
        }
    }
    return u;
}

void draw_list() {
    Browser& b = browser();
    ImGui::RadioButton("Loaded by the scene", &b.source, 0);
    ImGui::SameLine();
    if (ImGui::RadioButton("All files (gamedata + mods)", &b.source, 1) && !b.files_scanned) scan_files();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##texfilter", "filter by name", b.filter, sizeof(b.filter));
    const float row = ImGui::GetTextLineHeightWithSpacing();
    if (!ImGui::BeginChild("texlist", ImVec2(0, row * 12), ImGuiChildFlags_Borders)) {
        ImGui::EndChild();
        return;
    }
    int shown = 0;
    if (b.source == 0) {
        for (const scene::LoadedTexture& t : scene::loaded_textures()) {
            if (!matches(t.path, b.filter)) continue;
            ++shown;
            ImGui::PushID(shown);
            if (const Picture* p = scene_picture(t.texture)) {
                image_on_checker(*p, ImVec2(row - 2, row - 2));
                ImGui::SameLine();
            }
            char label[512];
            std::snprintf(label, sizeof(label), "%s  [p%d]%s", t.path.c_str(), t.palette, t.texture ? "" : "  (unreadable)");
            if (ImGui::Selectable(label, b.sel.path == t.path && b.sel.palette == t.palette)) select(t.path, t.palette);
            ImGui::PopID();
        }
    } else {
        ImGuiListClipper clip;
        std::vector<const FileEntry*> rows;
        for (const FileEntry& f : b.files)
            if (matches(f.path, b.filter)) rows.push_back(&f);
        shown = static_cast<int>(rows.size());
        clip.Begin(shown);
        while (clip.Step()) {
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const FileEntry& f = *rows[static_cast<std::size_t>(i)];
                ImGui::PushID(i);
                std::string label = f.path;
                if (!f.mod.empty()) label += "  [mod " + f.mod + "]";
                if (ImGui::Selectable(label.c_str(), b.sel.path == f.path)) select(f.path, palette_of(f.path));
                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();
    ImGui::TextDisabled("%d shown", shown);
    if (b.source == 1) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Rescan")) scan_files();
    }
}

void draw_selected() {
    Browser& b = browser();
    Selected& s = b.sel;
    if (s.path.empty()) {
        ImGui::TextDisabled("Pick a texture above (or a material in the World inspector).");
        return;
    }
    if (s.loaded && s.epoch != asset_epoch()) {
        // The store forgot it (reload from disk): read again.
        s.loaded = false;
        load_selected();
    }
    ImGui::SeparatorText("Texture");
    ImGui::TextWrapped("%s", s.path.c_str());
    const std::string host = host_file(s.path);
    const std::string owner = owner_mod(s.path);
    ImGui::TextDisabled("file: %s", host.empty() ? "(built from the indexed picture, or missing)" : host.c_str());
    ImGui::TextDisabled("from: %s", owner.empty() ? "base gamedata" : ("mod " + owner).c_str());
    if (!s.loaded) {
        ImGui::TextUnformatted("reading...");
        return;
    }
    if (!s.texture) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "cannot be read (see the log)");
        return;
    }
    const scene::SceneTexture& t = *s.texture;
    int holes = 0, semi = 0;
    for (std::size_t i = 3; i < t.rgba.size(); i += 4) {
        if (t.rgba[i] == 0) ++holes;
        else if (t.rgba[i] < 255) ++semi;
    }
    ImGui::Text("%d x %d   %s", t.width, t.height,
                s.bpp > 0 ? (std::to_string(s.bpp) + "-bit TIM, " + std::to_string(s.palettes) + " palette(s)").c_str()
                          : "true colour");
    if (!s.game_path.empty()) ImGui::TextDisabled("disc: %s", s.game_path.c_str());
    ImGui::TextDisabled("texels: %d transparent, %d semi-transparent (STP)", holes, semi);

    if (s.palettes > 1) {
        int row = s.palette < 0 ? 0 : s.palette;
        ImGui::SetNextItemWidth(160);
        if (ImGui::SliderInt("palette", &row, 0, s.palettes - 1)) select(variant_path(s.path, row), row, s.material);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", variant_path(s.path, row).substr(variant_path(s.path, row).rfind('/') + 1).c_str());
        // Which rows the map really draws with: the other rows of the file recolour texels that were not made for
        // them (odd colours and transparency), which is how the file is, not damage.
        // Taken once per file, before this slider asks the store for other rows itself.
        static std::string s_rows_of;
        static std::vector<int> s_rows;
        if (s_rows_of != base_of(s.path)) {
            s_rows_of = base_of(s.path);
            s_rows.clear();
            for (const scene::LoadedTexture& t : scene::loaded_textures())
                if (t.path == s_rows_of && t.palette >= 0 && std::find(s_rows.begin(), s_rows.end(), t.palette) == s_rows.end())
                    s_rows.push_back(t.palette);
            std::sort(s_rows.begin(), s_rows.end());
        }
        const bool used = std::find(s_rows.begin(), s_rows.end(), row) != s_rows.end();
        std::string rows;
        for (int r : s_rows) rows += (rows.empty() ? "" : ", ") + std::to_string(r);
        ImGui::TextDisabled(used ? "this colour table is used on the map" : "this colour table is NOT used on the map");
        if (!rows.empty()) ImGui::TextDisabled("used tables: %s", rows.c_str());
        ImGui::TextDisabled("(one picture, several colour tables: a material picks one; view only, the game is not changed)");
    }

    if (ImGui::Checkbox("Highlight in world", &b.highlight) && !b.highlight) scene::viewer_overrides().highlight_texture.clear();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    ImGui::SliderFloat("zoom", &b.zoom, 0.25f, 8.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
    if (const Picture* p = scene_picture(s.texture)) {
        const ImVec2 size(static_cast<float>(t.width) * b.zoom, static_cast<float>(t.height) * b.zoom);
        if (ImGui::BeginChild("preview", ImVec2(0, std::min(size.y + 16.0f, 520.0f)), ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            image_on_checker(*p, size);
            if (ImGui::IsItemHovered()) {
                const ImVec2 m = ImGui::GetMousePos(), o = ImGui::GetItemRectMin();
                const int x = static_cast<int>((m.x - o.x) / b.zoom), y = static_cast<int>((m.y - o.y) / b.zoom);
                if (x >= 0 && y >= 0 && x < t.width && y < t.height) {
                    const std::uint8_t* px = &t.rgba[(static_cast<std::size_t>(y) * t.width + x) * 4];
                    ImGui::SetTooltip("texel %d, %d\nRGB %u %u %u  alpha %u", x, y, px[0], px[1], px[2], px[3]);
                }
            }
        }
        ImGui::EndChild();
    }

    const Users u = users_of(s.material);
    ImGui::SeparatorText("Used by (what the scene has read)");
    auto list = [](const char* what, const std::vector<std::string>& names) {
        if (names.empty()) return;
        if (ImGui::TreeNode(what, "%s (%zu)", what, names.size())) {
            for (const std::string& n : names) ImGui::BulletText("%s", n.c_str());
            ImGui::TreePop();
        }
    };
    list("cells", u.cells);
    list("models", u.models);
    list("characters", u.characters);
    if (u.cells.empty() && u.models.empty() && u.characters.empty()) ImGui::TextDisabled("nothing loaded uses it");
}

void draw_browser() {
    browser().drawn_frame = ImGui::GetFrameCount();
    static bool s_placed = false;
    if (!s_placed) {
        // A first size and place, once: the host opens a user-sized panel with no height.
        s_placed = true;
        const ImVec2 screen = ImGui::GetMainViewport()->Size;
        const float scale = std::max(1.0f, screen.y / 720.0f);
        ImGui::SetWindowSize(ImVec2(std::min(540.0f * scale, screen.x * 0.65f), screen.y * 0.9f), ImGuiCond_Always);
        ImGui::SetWindowPos(ImVec2(screen.x - std::min(540.0f * scale, screen.x * 0.65f) - 8.0f, screen.y * 0.05f),
                            ImGuiCond_Always);
    }
    Browser& b = browser();
    if (ImGui::Button("Reload textures/models from disk")) reload_from_disk();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Reads the mods' files again (added, changed or removed PNG and glb): the world, the\n"
                          "characters and these panels show the new pictures and models.");
    draw_list();
    draw_selected();
    // The highlight follows the picked texture while the box is ticked.
    std::string& hl = scene::viewer_overrides().highlight_texture;
    if (b.highlight && !b.sel.material.empty()) hl = b.sel.material;
    else if (!b.highlight) hl.clear();
}

}  // namespace

void browse_texture(const std::string& path, int palette) {
    select(path, palette);
    run_command("editorpanel tools.textures on");
}

namespace {

// The browser's commands for scripted runs (-consolecmd): pick a texture, the world highlight, reload from disk.
void register_texture_commands() {
    using sh::console::ArgSpec;
    using sh::console::ArgType;
    sh::console::Registry& r = sh::console::global_registry();
    sh::console::CommandSpec tex;
    tex.name = "editortexture";
    tex.group = "debug";
    tex.help = "show a texture in the editor's texture browser (gamedata path, or 'files' / 'loaded' for the list)";
    tex.args = {ArgSpec{"path", ArgType::String, false, "convert/textures/... .png, files, loaded", {}, {}},
                ArgSpec{"zoom", ArgType::Float, true, "preview zoom", {}, {}}};
    tex.run = [](sh::console::Context&, const sh::console::Args& args) -> Result<void> {
        Browser& b = browser();
        const std::string& what = args.str(0);
        if (what == "files") {
            b.source = 1;
            scan_files();
        } else if (what == "loaded") {
            b.source = 0;
        } else {
            select(what, palette_of(what));
        }
        if (args.has(1)) b.zoom = static_cast<float>(args.number(1));
        run_command("editorpanel tools.textures on");
        return Result<void>::ok();
    };
    (void)r.add(std::move(tex));

    sh::console::CommandSpec filter;
    filter.name = "editortexfilter";
    filter.group = "debug";
    filter.help = "set the texture browser's name filter";
    filter.args = {ArgSpec{"text", ArgType::String, true, "part of the path; omitted clears", {}, {}}};
    filter.run = [](sh::console::Context&, const sh::console::Args& args) -> Result<void> {
        std::snprintf(browser().filter, sizeof(browser().filter), "%s", args.str(0).c_str());
        return Result<void>::ok();
    };
    (void)r.add(std::move(filter));

    sh::console::CommandSpec hl;
    hl.name = "editorhighlight";
    hl.group = "debug";
    hl.help = "tint every surface drawn with the texture browser's texture (1) or stop (0)";
    hl.args = {ArgSpec{"state", ArgType::Bool, false, "on/off", {}, {}}};
    hl.run = [](sh::console::Context&, const sh::console::Args& args) -> Result<void> {
        browser().highlight = args.boolean(0);
        return Result<void>::ok();
    };
    (void)r.add(std::move(hl));

    sh::console::CommandSpec reload;
    reload.name = "editorreload";
    reload.group = "debug";
    reload.help = "read textures and models again from disk (the mods' files included), as the editor's button";
    reload.run = [](sh::console::Context& ctx, const sh::console::Args&) -> Result<void> {
        reload_from_disk();
        ctx.out.info("editorreload: textures and models are read again at the next update");
        return Result<void>::ok();
    };
    (void)r.add(std::move(reload));
}

struct Install {
    Install() {
        Panel p;
        p.id = "tools.textures";
        p.title = "Textures";
        p.menu = "Assets";
        p.draw = &draw_browser;
        p.open = false;
        p.auto_size = false;
        register_panel(std::move(p));
        register_texture_commands();
        // The world highlight lasts while the browser is shown: closed, it lets the world go.
        Overlay o;
        o.id = "tools.texture_highlight";
        o.label = "Texture highlight (with the Textures panel)";
        o.enabled = true;
        o.draw = [](Draw3D&) {
            if (ImGui::GetFrameCount() - browser().drawn_frame > 1) scene::viewer_overrides().highlight_texture.clear();
        };
        register_overlay(std::move(o));
    }
};

const Install s_install;

}  // namespace

}  // namespace sh::tools::assets
