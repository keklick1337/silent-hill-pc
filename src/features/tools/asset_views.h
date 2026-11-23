// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Shared by the asset panels of the editor: the texture browser
// (texture_viewer.cpp), the world inspector (world_inspect.cpp) and the model viewer (model_viewer.cpp). Pictures
// shown in ImGui live in the engine overlay's texture store; the panels open each other's views through the calls
// below.

#pragma once

#include <imgui.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sh::scene {
struct SceneTexture;
}
namespace sh::assets {
class SkinModel;
}

namespace sh::tools::assets {

// An RGBA picture as an ImGui texture, remade when set() is called again; released with the object.
class Picture {
public:
    Picture() = default;
    Picture(const Picture&) = delete;
    Picture& operator=(const Picture&) = delete;
    ~Picture();
    // `scene_alpha`: the pixels are a scene texture (alpha 0 hole, 128 semi-transparent, 255 solid); the
    // semi-transparent texels are shown half covering (alpha 0.5 over the checkerboard).
    void set(int width, int height, const std::uint8_t* rgba, bool scene_alpha = false);
    void clear();
    bool valid() const { return id_ != 0; }
    ImTextureID id() const { return static_cast<ImTextureID>(id_); }
    int width() const { return w_; }
    int height() const { return h_; }

private:
    std::uint32_t id_ = 0;
    int w_ = 0, h_ = 0;
};

// A loaded scene texture's picture, cached by the texture until the store forgets its assets.
const Picture* scene_picture(const std::shared_ptr<const scene::SceneTexture>& texture);

// A checkerboard under [p0, p1) of the window's draw list (what the transparent texels show).
void checkerboard(ImDrawList* list, ImVec2 p0, ImVec2 p1, float cell = 8.0f);
// `picture` at `size` over a checkerboard; the item is the picture's rectangle.
void image_on_checker(const Picture& picture, ImVec2 size);

// Host file of a gamedata path (the mod's copy first) and the mod that owns it ("" for the base data).
std::string host_file(const std::string& relative);
std::string owner_mod(const std::string& relative);

// A character's skinned model (chara/<folder>/character.json "model"), read at an update on first ask: null until
// then, or when it cannot be read (`error` says why). Its textures as pictures by SkinMaterial::texture.
const sh::assets::SkinModel* character_model(const std::string& folder, std::string* error = nullptr);
const Picture* character_texture(const std::string& folder, int texture);
// The host file the character's model is read from (a mod's copy first), empty when none has it.
std::string character_model_file(const std::string& folder);

// The other panels' doors.
void browse_texture(const std::string& path, int palette);
void view_character(const std::string& folder);
void view_library_model(const std::string& file, const std::string& name);

// "Reload textures/models from disk": the mods' file index is built again (files added or removed), the scene
// store forgets every cell, model and texture (the renderers drop their GPU copies, the characters and the item
// models read their files again) and the panels' own caches go. Runs at the next update.
void reload_from_disk();
// Counts reload_from_disk() runs and the store's other forgets: what a panel compares its caches with.
std::uint32_t asset_epoch();

}  // namespace sh::tools::assets
