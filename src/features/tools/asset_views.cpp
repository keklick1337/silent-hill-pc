// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The asset panels' shared pieces (asset_views.h): pictures in the overlay's texture store, the checkerboard, host
// files and owners of gamedata paths, and the reload from disk.

#include "features/tools/asset_views.h"
#include "features/tools/tools.h"

#include <map>

#include "engine/mods/registry.h"
#include "engine/vfs/vfs_c.h"
#include "render/overlay/overlay.h"
#include "render/scene/area_cells.h"
#include "render/scene/scene.h"

namespace sh::tools::assets {

namespace {

std::uint32_t s_reloads = 0;

}  // namespace

Picture::~Picture() { clear(); }

void Picture::clear() {
    if (id_ != 0) overlay::overlay().textures().destroy(id_);
    id_ = 0;
    w_ = h_ = 0;
}

void Picture::set(int width, int height, const std::uint8_t* rgba, bool scene_alpha) {
    if (width <= 0 || height <= 0 || rgba == nullptr) {
        clear();
        return;
    }
    std::vector<std::uint8_t> shown;
    const std::uint8_t* pixels = rgba;
    if (scene_alpha) {
        shown.assign(rgba, rgba + static_cast<std::size_t>(width) * height * 4);
        for (std::size_t i = 3; i < shown.size(); i += 4) shown[i] = shown[i] == 0 ? 0 : (shown[i] < 255 ? 128 : 255);
        pixels = shown.data();
    }
    overlay::TextureStore& store = overlay::overlay().textures();
    if (id_ != 0 && w_ == width && h_ == height) {
        store.update(id_, width, height, pixels);
        return;
    }
    clear();
    id_ = store.create(width, height, overlay::Format::Rgba8, overlay::Filter::Nearest, pixels);
    if (id_ != 0) {
        w_ = width;
        h_ = height;
    }
}

const Picture* scene_picture(const std::shared_ptr<const scene::SceneTexture>& texture) {
    struct Entry {
        std::weak_ptr<const scene::SceneTexture> source;
        std::unique_ptr<Picture> picture;
    };
    static std::map<const scene::SceneTexture*, Entry> s_cache;
    static std::uint32_t s_epoch = asset_epoch();
    if (s_epoch != asset_epoch()) {
        s_cache.clear();
        s_epoch = asset_epoch();
    }
    if (!texture || texture->width <= 0 || texture->height <= 0) return nullptr;
    Entry& e = s_cache[texture.get()];
    if (!e.picture || e.source.expired()) {
        e.source = texture;
        e.picture = std::make_unique<Picture>();
        e.picture->set(texture->width, texture->height, texture->rgba.data(), true);
    }
    return e.picture.get();
}

void checkerboard(ImDrawList* list, ImVec2 p0, ImVec2 p1, float cell) {
    const ImU32 a = IM_COL32(90, 90, 90, 255), b = IM_COL32(150, 150, 150, 255);
    list->PushClipRect(p0, p1, true);
    int row = 0;
    for (float y = p0.y; y < p1.y; y += cell, ++row) {
        int col = row;
        for (float x = p0.x; x < p1.x; x += cell, ++col)
            list->AddRectFilled(ImVec2(x, y), ImVec2(x + cell, y + cell), (col & 1) ? a : b);
    }
    list->PopClipRect();
}

void image_on_checker(const Picture& picture, ImVec2 size) {
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    checkerboard(ImGui::GetWindowDrawList(), p0, p1, size.x < 64 ? 4.0f : 8.0f);
    ImGui::Image(ImTextureRef(picture.id()), size);
}

std::string host_file(const std::string& relative) {
    char out[1024];
    if (ShVfs_FindDataFile(relative.c_str(), out, sizeof(out))) return out;
    return {};
}

std::string owner_mod(const std::string& relative) { return sh::mods::registry().owner_of(relative); }

std::uint32_t asset_epoch() { return s_reloads + scene::asset_generation(); }

void reload_from_disk() {
    defer([] {
        if (sh::mods::registry().configured()) sh::mods::registry().reindex();
        scene::forget_assets();
        ++s_reloads;
    });
}

}  // namespace sh::tools::assets
