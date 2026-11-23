#pragma once

#include "formats/common/status.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sh::gltf {

struct MeshStats {
    std::string name;
    std::size_t primitives = 0;
    std::size_t vertices = 0;          // sum of POSITION accessor counts
    std::size_t triangles = 0;         // sum of index counts / 3
    std::size_t unique_positions = 0;  // distinct POSITION values, bit-exact, over all primitives of the mesh
    std::array<float, 3> min{};        // bounding box of the POSITION values, read from the data (not the accessor)
    std::array<float, 3> max{};
    std::vector<float> triangle_positions;  // 9 floats per triangle in index order; only with keep_triangles
};

struct GlbStats {
    std::vector<MeshStats> meshes;
    std::size_t nodes = 0;
    std::size_t materials = 0;
    std::size_t skins = 0;
    std::size_t animations = 0;
    std::vector<std::string> image_uris;
    std::size_t vertices = 0;
    std::size_t triangles = 0;
};

// Reads a .glb with cgltf (an independent reader), runs cgltf_validate, and walks every vertex attribute and index
// so a bad accessor shows up as an error rather than a crash in a consumer. External images are not opened.
// Fails with cgltf's verdict as text.
Result<GlbStats> inspect_glb(std::span<const std::uint8_t> glb, bool keep_triangles = false);

// The node tree, skins, animations and skinned vertex data of a .glb as cgltf reads them, for checks that pose a
// skeleton independently of the writer (tests compare it with the game's own bone arithmetic).
struct GlbNodeData {
    std::string name;
    int parent = -1;
    int mesh = -1;
    int skin = -1;
    std::array<float, 3> translation{0, 0, 0};
    std::array<float, 4> rotation{0, 0, 0, 1};  // xyzw
    std::array<float, 3> scale{1, 1, 1};
    bool has_matrix = false;
    std::array<float, 16> matrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // column-major
};
struct GlbSkinData {
    std::vector<int> joints;
    std::vector<std::array<float, 16>> inverse_bind;  // column-major; identity when the file has none
};
struct GlbChannelData {
    int node = -1;
    std::string path;           // "translation", "rotation", "scale"
    std::string interpolation;  // "LINEAR", "STEP", "CUBICSPLINE"
    std::vector<float> times;
    std::vector<float> values;  // 3 or 4 per key
};
struct GlbAnimationData {
    std::string name;
    std::vector<GlbChannelData> channels;
};
struct GlbPrimitiveData {
    int mesh = -1;
    std::vector<float> positions;       // xyz
    std::vector<std::uint16_t> joints;  // 4 per vertex, empty when not skinned
    std::vector<float> weights;         // 4 per vertex
};
struct GlbSceneData {
    std::vector<GlbNodeData> nodes;
    std::vector<GlbSkinData> skins;
    std::vector<GlbAnimationData> animations;
    std::vector<GlbPrimitiveData> primitives;
};

// Parses and validates like inspect_glb, then copies out the data above.
Result<GlbSceneData> read_glb_scene(std::span<const std::uint8_t> glb);

}  // namespace sh::gltf
