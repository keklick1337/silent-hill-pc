#pragma once

// Model previews for the UI (the Debug -> Spawn turntable): a skinned character drawn in its file's bind pose (the
// importer bakes the first keyframe of the character's animation into it), turned about its vertical axis and lit by
// a fixed light, into a rectangle of the screen with its own camera. The meshes go into current_frame().prelit as
// isolated posed meshes (scene.h), so the frame draws them at the scene points the caller links, like the item
// previews of the inventory: first a depth reset of the rectangle, then the model, then whatever the caller draws
// after them (the menu) stays on top.

#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sh::scene {

struct PreviewModel {
    struct Group {
        std::uint64_t texture_key = 0;  // 0 = untextured
        bool blended = false;           // glTF alpha mode BLEND
        std::array<float, 3> color{1, 1, 1};
        std::vector<std::uint32_t> indices;
    };
    // Game axes (x right, y down, z forward, 256 units per metre), centred on the bounding box.
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 3>> normals;  // unit, outward
    std::vector<std::array<float, 2>> uvs;
    std::vector<Group> groups;
    float radius_xz = 1;      // farthest vertex from the vertical axis through the centre
    float half_height = 1;    // half the bounding box height
};

// Reads an image the glTF names, by its path resolved against the glTF's folder (the caller looks mods up and
// builds palette variants). Empty when there is none: the material draws untextured.
using PreviewImageReader = std::function<std::optional<std::vector<std::uint8_t>>(const std::filesystem::path& resolved)>;

// Loads a skinned glTF (only the first skin's meshes count, as for the characters in the world) and registers its
// textures for the posed meshes (add_prelit_texture). `rotate_deg` turns the bind pose about the game's x, then y,
// then z axis (degrees) before it is centred: a bind pose that lies or leans stands up (character.json "preview").
Result<std::shared_ptr<const PreviewModel>> load_preview_model(const std::filesystem::path& glb,
                                                               const PreviewImageReader& read_image,
                                                               const std::array<float, 3>& rotate_deg = {0, 0, 0});

// Where the preview goes: a rectangle in the screen units of the frame's 2D projection, relative to the draw-area
// offset (the coordinates the PS1 prims of the menu use), and how much taller a unit is drawn than wide (2 on an
// interlaced 448-line screen).
struct PreviewPlacement {
    float x = 0, y = 0, w = 0, h = 0;
    float y_scale = 1;
};

// Appends the preview of `model` turned by `yaw` radians (0 faces the viewer) to current_frame().prelit and returns
// the indices of the added meshes in draw order (the depth reset first). Empty when there is nothing to draw.
// `zoom` scales the fitted model (1 = fit the rectangle); `lift` moves it up by that fraction of the rectangle.
std::vector<int> add_preview(const PreviewModel& model, const PreviewPlacement& where, float yaw, float zoom = 1.0f,
                             float lift = 0.0f);

}  // namespace sh::scene
