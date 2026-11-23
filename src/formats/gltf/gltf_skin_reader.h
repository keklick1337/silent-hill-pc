#pragma once

#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// The skinned meshes of a glTF (.glb, or .gltf with its buffers) as plain data, for the engine's skinned characters
// (engine/assets/skin_model.h). Only the first skin counts; mesh nodes of other skins and meshes without a skin are
// left out. Values are as the file stores them (glTF axes, metres); images are not decoded.
namespace sh::gltf {

struct SkinJointData {
    std::string name;
    int parent = -1;                   // index of the nearest ancestor that is a joint of the skin, or -1
    std::array<float, 16> inverse_bind{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // column-major
    std::string extras;                // the node's extras JSON, or empty
};

enum class SkinAlpha : std::uint8_t { Opaque, Mask, Blend };

struct SkinMaterialData {
    std::string name;
    std::array<float, 4> base_color{1, 1, 1, 1};
    SkinAlpha alpha = SkinAlpha::Opaque;
    float alpha_cutoff = 0.5f;
    bool double_sided = false;
    // Base colour image: an external file (uri, percent-decoded, relative to the glTF) or bytes stored in the file.
    std::string image_uri;
    std::vector<std::uint8_t> image_bytes;
    std::string extras;
};

struct SkinPrimitiveData {
    std::string node;      // name of the mesh node (the ILM part for imported characters)
    std::string node_extras;  // the mesh node's extras JSON, or empty
    int material = -1;
    std::vector<float> positions;        // xyz
    std::vector<float> normals;          // xyz, empty when the file has none
    std::vector<float> uvs;              // uv, empty when the file has none
    std::vector<std::uint16_t> joints;   // 4 per vertex, indices into SkinnedGltf::joints
    std::vector<float> weights;          // 4 per vertex
    std::vector<std::uint32_t> indices;  // triangles
};

struct SkinnedGltf {
    std::string asset_extras;  // the extras JSON of the file's "asset", or empty
    std::vector<SkinJointData> joints;
    std::vector<SkinMaterialData> materials;
    std::vector<SkinPrimitiveData> primitives;
};

// Fails when the file cannot be parsed or validated, has no skin, or a skinned primitive is not a triangle list
// with POSITION, JOINTS_0 and WEIGHTS_0.
Result<SkinnedGltf> read_skinned_gltf(const std::filesystem::path& file);

}  // namespace sh::gltf
