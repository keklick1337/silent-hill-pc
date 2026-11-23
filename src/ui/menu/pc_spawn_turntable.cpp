// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/menu/pc_spawn_turntable.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <vector>
#include <string>

#include "engine/chara/chara_library.h"
#include "engine/log/sh_log.h"
#include "engine/vfs/vfs_c.h"
#include "formats/png/palette_variant.h"
#include "engine/chara/runtime/chara_data.h"
#include "render/scene/preview.h"
#include "render/scene/scene.h"
#include "render/scene/scene_c.h"
#include "ui/menu/nm_draw.h"

namespace {

namespace fs = std::filesystem;

std::string s_browsed;  // under the dropdown cursor
std::string s_picked;   // the pick, while the Preview row is on
const char* (*s_picker)(void) = nullptr;
bool s_onPage = false;
std::string s_current;  // what is shown: s_browsed, else s_picked on the Spawn page
int s_angle = 0;        // Q12 turns
constexpr int kStepPerFrame = 4096 / 16 / 60;  // 1/16 turn per second at 60 frames per second

// The panel on the right of the menu, in menu coordinates (the 320 x 240 space of ui/menu/nm_draw.h).
constexpr int kPanelX = 214, kPanelY = 52, kPanelW = 100, kPanelH = 150;

std::optional<std::vector<std::uint8_t>> read_host_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The part of `path` below its last "convert" folder, or empty.
fs::path below_convert(const fs::path& path) {
    const fs::path norm = path.lexically_normal();
    std::vector<fs::path> parts(norm.begin(), norm.end());
    for (std::size_t i = parts.size(); i-- > 0;) {
        if (parts[i] != "convert") continue;
        fs::path rel;
        for (std::size_t k = i + 1; k < parts.size(); ++k) rel /= parts[k];
        return rel;
    }
    return {};
}

// A texture the glb names: a mod's copy first (by its path below convert/), palette variants built from the
// indexed picture, as for the characters in the world.
std::optional<std::vector<std::uint8_t>> read_image(const fs::path& resolved) {
    fs::path chosen = resolved;
    if (const fs::path rel = below_convert(resolved); !rel.empty()) {
        char host[1024];
        if (ShVfs_FindConverted(rel.generic_string().c_str(), host, sizeof host)) chosen = host;
    }
    return sh::png::read_texture_or_variant(chosen.generic_string(), read_host_file);
}

// Loaded models by folder name; a failed load is kept as null and not tried again.
std::shared_ptr<const sh::scene::PreviewModel> model_for(const std::string& folder) {
    static std::map<std::string, std::shared_ptr<const sh::scene::PreviewModel>> s_models;
    static std::uint32_t s_generation = sh::scene::asset_generation();
    if (s_generation != sh::scene::asset_generation()) {
        s_models.clear();
        s_generation = sh::scene::asset_generation();
    }
    if (auto it = s_models.find(folder); it != s_models.end()) return it->second;
    std::shared_ptr<const sh::scene::PreviewModel> model;
    const sh::chara::CharaLibrary* lib = Pc_CharaData_Library();
    const sh::chara::CharacterDef* def = lib != nullptr ? lib->find_by_id(folder) : nullptr;
    char host[1024];
    if (def == nullptr || def->model.empty()) {
        SH_WARN("[SPAWN] turntable: %s has no model in its character.json", folder.c_str());
    } else if (!ShVfs_FindConverted(def->model.c_str(), host, sizeof host)) {
        SH_WARN("[SPAWN] turntable: %s: %s is not in gamedata/convert or a mod", folder.c_str(), def->model.c_str());
    } else if (auto loaded = sh::scene::load_preview_model(
                   host, read_image,
                   {static_cast<float>(def->preview_rotate[0]), static_cast<float>(def->preview_rotate[1]),
                    static_cast<float>(def->preview_rotate[2])});
               !loaded) {
        SH_WARN("[SPAWN] turntable: %s: %s", host, loaded.error().c_str());
    } else {
        model = loaded.value();
    }
    s_models.emplace(folder, model);
    return model;
}

void refresh() {
    const std::string next = !s_browsed.empty() ? s_browsed : (s_onPage ? s_picked : std::string());
    if (next != s_current) s_angle = 0;  // a new character starts facing the viewer
    s_current = next;
}

}  // namespace

extern "C" void Pc_SpawnTurntable_Show(const char* name) {
    s_browsed = name != nullptr ? name : "";
    refresh();
}

extern "C" void Pc_SpawnTurntable_Hide(void) {
    s_browsed.clear();
    refresh();
}

extern "C" void Pc_SpawnTurntable_SetPicker(const char* (*picked)(void)) { s_picker = picked; }

extern "C" void Pc_SpawnTurntable_SetPage(int onSpawnPage) {
    s_onPage = onSpawnPage != 0;
    refresh();
}

// The engine's scene renderer draws the posed meshes.
extern "C" int Pc_SpawnTurntable_Available(void) { return 1; }

extern "C" void Pc_SpawnTurntable_Draw(void) {
    if (s_picker != nullptr) {
        const char* picked = s_picker();
        s_picked = picked != nullptr ? picked : "";
        refresh();
    }
    if (s_current.empty()) return;
    s_angle = (s_angle + kStepPerFrame) & 0xFFF;
    if (!Pc_SpawnTurntable_Available()) return;
    const auto model = model_for(s_current);
    if (!model) return;
    sh::scene::PreviewPlacement where;
    const sh::ui::nm::FramePoint corner = sh::ui::nm::to_frame(kPanelX, kPanelY);
    const sh::ui::nm::FramePoint opposite = sh::ui::nm::to_frame(kPanelX + kPanelW, kPanelY + kPanelH);
    where.x = corner.x;
    where.y = corner.y;
    where.y_scale = corner.y_scale;
    where.w = opposite.x - corner.x;
    where.h = opposite.y - corner.y;
    const float yaw = static_cast<float>(s_angle) * (6.28318531f / 4096.0f);
    float zoom = 1.0f, lift = 0.0f;
    const sh::chara::CharaLibrary* lib = Pc_CharaData_Library();
    if (const sh::chara::CharacterDef* def = lib != nullptr ? lib->find_by_id(s_current) : nullptr) {
        zoom = static_cast<float>(def->preview_zoom);
        lift = static_cast<float>(def->preview_lift);
    }
    const std::vector<int> items = sh::scene::add_preview(*model, where, yaw, zoom, lift);
    // A bucket draws the packet linked last first: linked in reverse, the depth reset goes first.
    for (auto it = items.rbegin(); it != items.rend(); ++it) sh::ui::nm::link_scene_item(*it);
}
