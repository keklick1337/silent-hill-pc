#pragma once

#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// glTF 2.0 binary (.glb) writer.
//
// The caller fills a gltf::Document with plain arrays (positions, normals, indices, node tree, materials) and calls
// write_glb(), which lays out the single binary buffer, the bufferViews and accessors, and the JSON chunk. Nothing
// is parsed back; the independent check lives in gltf_inspect.h (cgltf).
//
// Conventions of the produced file (glTF 2.0 core):
//   - right-handed, +Y up, metres; triangles are counter-clockwise when seen from the front;
//   - texture coordinates have their origin at the top-left corner of the image, V grows downward (the same as the
//     PS1 texture page), so no V flip is needed when the source is a PS1 texture;
//   - textures are external files referenced by a relative URI (for example "textures/CHARA/HERO.png"); the writer
//     never reads or embeds them.
//
// Indices are always triangle lists. Index accessors are UNSIGNED_SHORT when the primitive has fewer than 65535
// vertices and UNSIGNED_INT otherwise.
namespace sh::gltf {

// A texture image the document refers to by relative URI. The same URI is registered once.
struct Texture {
    std::string uri;
    bool nearest = true;  // NEAREST filtering (PS1 look); false gives LINEAR
};

enum class AlphaMode : std::uint8_t { Opaque, Mask, Blend };

struct Material {
    std::string name;
    std::array<float, 4> base_color = {1.0f, 1.0f, 1.0f, 1.0f};
    int texture = -1;  // index into Document::textures, or -1
    float metallic = 0.0f;
    float roughness = 1.0f;
    AlphaMode alpha_mode = AlphaMode::Opaque;
    float alpha_cutoff = 0.5f;
    bool double_sided = false;
    std::string extras;  // JSON object text, or empty
};

// One draw call: a triangle list with its vertex attributes. All present attribute arrays must have the same
// vertex count; indices must reference existing vertices and their count must be a multiple of 3.
struct Primitive {
    std::vector<float> positions;   // xyz per vertex (required)
    std::vector<float> normals;     // xyz per vertex, unit length (optional)
    std::vector<float> texcoords;   // uv per vertex (optional)
    std::vector<std::uint8_t> colors;  // rgba per vertex, 8-bit normalised (optional)
    std::vector<std::uint16_t> joints;   // 4 joint indices per vertex (optional, with weights)
    std::vector<float> weights;          // 4 weights per vertex (optional, with joints)
    std::vector<std::uint32_t> indices;  // triangle list (required)
    int material = -1;                   // index into Document::materials, or -1 for the default material
    std::string extras;                  // JSON object text, or empty

    std::size_t vertex_count() const { return positions.size() / 3; }
};

struct Mesh {
    std::string name;
    std::vector<Primitive> primitives;
    std::string extras;
};

// Node transform: either a 4x4 column-major matrix, or translation/rotation(xyzw quaternion)/scale. The default is
// the identity and writes nothing.
struct Node {
    std::string name;
    std::vector<int> children;  // indices into Document::nodes
    int mesh = -1;
    int skin = -1;
    bool has_matrix = false;
    std::array<float, 16> matrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::array<float, 3> translation = {0, 0, 0};
    std::array<float, 4> rotation = {0, 0, 0, 1};
    std::array<float, 3> scale = {1, 1, 1};
    std::string extras;
    std::string extensions;  // "extensions" object of the node, JSON object text, or empty
};

struct Skin {
    std::string name;
    std::vector<int> joints;                       // node indices
    std::vector<std::array<float, 16>> inverse_bind;  // column-major, one per joint, or empty for identity
    int skeleton = -1;                             // root node of the skeleton, or -1
};

// A scene after the default one (scene 0). Its roots must be parentless nodes; a node may be a root of several scenes.
struct Scene {
    std::string name;
    std::vector<int> roots;
    std::string extras;  // JSON object text, or empty
};

// Keyframe animation (glTF 2.0 core). One sampler maps key times (seconds, strictly increasing) to values: VEC3
// for translation and scale, VEC4 (xyzw unit quaternion) for rotation. CUBICSPLINE is not offered.
enum class Interpolation : std::uint8_t { Linear, Step };
enum class AnimationPath : std::uint8_t { Translation, Rotation, Scale };

struct AnimationSampler {
    std::vector<float> input;   // key times in seconds
    std::vector<float> output;  // 3 or 4 floats per key, by the path of the channels that use the sampler
    Interpolation interpolation = Interpolation::Linear;
};

struct AnimationChannel {
    int sampler = -1;  // index into Animation::samplers
    int node = -1;     // target node
    AnimationPath path = AnimationPath::Rotation;
};

// A named clip. Samplers of one animation with equal key times share one input accessor in the file.
struct Animation {
    std::string name;
    std::vector<AnimationSampler> samplers;
    std::vector<AnimationChannel> channels;
    std::string extras;
};

struct Document {
    std::string generator = "silent-hill-pc";
    std::string extras;  // asset-level extras, JSON object text, or empty
    // Root "extensions" object (JSON object text, or empty) and the names listed in "extensionsUsed".
    std::string extensions;
    std::vector<std::string> extensions_used;
    // Opaque byte blocks for an extension to point at: blob i is bufferView i (blobs are laid out before any
    // accessor data), each at a 4-byte boundary of the BIN chunk. A blob must not be empty.
    std::vector<std::vector<std::uint8_t>> blobs;
    std::vector<Texture> textures;
    std::vector<Material> materials;
    std::vector<Mesh> meshes;
    std::vector<Node> nodes;
    std::vector<Skin> skins;
    std::vector<Animation> animations;
    std::vector<int> scene_roots;  // indices of the nodes in the default scene
    std::string scene_name;
    std::vector<Scene> scenes;  // scenes 1.. (optional)
};

// Serialises the document. Fails with a message when the document is inconsistent (index out of range, attribute
// arrays of different length, a triangle list that is not a multiple of 3, a node tree that is not a forest, ...).
Result<std::vector<std::uint8_t>> write_glb(const Document& doc);

}  // namespace sh::gltf
