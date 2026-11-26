#pragma once

// A skinned glTF character in the game's space (model_source = gltf): the imported convert/models/CHARA/<NAME>.glb
// or a mod's HD model with the same joint names. The game keeps posing its own bones (ANM, Anim_BonesUpdate); every
// vertex follows them through the file's skin weights.
//
// Joints are game bones by name: "boneNN" (the importer's names, NN the ANM bone index; extras {"bone": NN} also
// works). A joint without a bone (a twist or finger joint of an HD rig) moves rigidly with its nearest ancestor that
// has one. For each influence the vertex is stored in that bone's local space in game units (Y down, 256 per metre:
// x = -256 x', y = -256 y', z = 256 z' of the glTF bind-local position), so posing is W_bone * local, the arithmetic
// the game uses for the ILM vertices of the bone's part.

#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace sh::assets {

struct SkinInfluence {
    int bone = 0;
    float weight = 0.0f;
    std::array<float, 3> local{0, 0, 0};
};

struct SkinVertex {
    std::array<SkinInfluence, 4> influence{};
    int count = 0;
    int dominant = 0;                        // the bone with the largest weight; lighting is done in its space
    std::array<float, 3> normal{0, 0, -1};   // outward, about unit, in the dominant bone's local space, game axes
    std::array<float, 2> uv{0, 0};
};

enum class SkinAlphaMode : std::uint8_t { Opaque, Mask, Blend };

struct SkinTexture {
    std::filesystem::path file;  // empty for an image stored in the glb
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

struct SkinMaterial {
    std::string name;
    std::array<float, 4> base_color{1, 1, 1, 1};
    SkinAlphaMode alpha = SkinAlphaMode::Opaque;
    float alpha_cutoff = 0.5f;
    bool semi_transparent = false;  // extras "semi_transparent" of the importer (PS1 STP blending)
    // extras "abr" (0..3, ps1_blend.h) when the file fixes the mode; -1: the character's mode at run time (the
    // importer's characters: the mode belongs to the character type, not to the model file).
    int abr = -1;
    bool double_sided = false;
    int texture = -1;               // index into SkinModel::textures
};

// The triangles of one mesh node with one material.
struct SkinGroup {
    std::string node;   // the mesh node: the ILM part name in an imported model
    int part = -1;      // the node's extras "part_index" (the ILM part it was made from), -1 when it has none
    int material = -1;
    std::vector<std::uint32_t> indices;
};

// Bone transform for posing: row-major 3x3 rotation (unit scale, the game's Q12 / 4096) and translation in game units.
struct BoneTransform {
    std::array<float, 9> m{1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::array<float, 3> t{0, 0, 0};
};

// How the animation places one bone: under which bone, and either with a translation its clips write (`animated`:
// the root bones) or at a fixed rest offset in the parent's space (game units).
struct BoneRest {
    int parent = -1;
    bool animated = true;
    std::array<float, 3> offset{0, 0, 0};
};

class SkinModel {
public:
    std::vector<SkinVertex> vertices;
    std::vector<SkinGroup> groups;
    std::vector<SkinMaterial> materials;
    std::vector<SkinTexture> textures;
    int bone_count = 0;  // 1 + the largest bone a vertex uses

    // A model with its own proportions (the file's asset extras {"proportions": "model"}): `rest[b]` is where the
    // joint of bone b sits in the space of its parent bone `rest_parent[b]` in the file's bind pose (game units), for
    // every bone `has_rest[b]` marks. The drawer puts these in place of the animation's rest offsets
    // (render/skinning/gltf_chara.h), so the game's clips turn the model's own bone lengths. Empty for a model on
    // the game's skeleton as it is.
    bool own_proportions = false;
    std::vector<std::array<float, 3>> rest;
    std::vector<int> rest_parent;
    std::vector<std::uint8_t> has_rest;
    // Where the joints of an own-proportions model are against the animation's skeleton `rest` (`count` bones) in
    // the pose whose bone rotations `rotation(bone, out)` gives (row-major 3x3, any one scale `unit`: 4096 for
    // Q12): a bone with a fixed rest offset takes the model's offset instead, turned by its parent; a bone the
    // clips move stays where they put it. `out[b]` is the difference in the pose's space; all zero for a model on
    // the game's skeleton.
    void joint_shifts(const BoneRest* rest, int count, double unit,
                      const std::function<void(int bone, double out[9])>& rotation,
                      std::array<double, 3>* out) const;

    // Game-space positions under `bones` (indexed by game bone). A vertex whose bones are all missing (`have` 0, or
    // past the vector) gets ok = 0. Influences of missing bones are dropped and the rest renormalised.
    void pose(const std::vector<BoneTransform>& bones, const std::vector<std::uint8_t>& have, std::vector<float>& xyz,
              std::vector<std::uint8_t>& ok) const;
    // The same into caller buffers of vertices.size() * 3 floats and vertices.size() flags.
    void pose(const BoneTransform* bones, std::size_t bone_count, const std::uint8_t* have, float* xyz,
              std::uint8_t* ok) const;
};

// The file of an image the glTF names: `resolved` is the uri resolved against the glTF's folder. The engine's
// resolver looks the part below convert/ up in the mods first, so an HD texture of a mod replaces the imported PNG
// even when the glb is the imported one.
using TextureResolver = std::function<std::filesystem::path(const std::filesystem::path& resolved)>;

// The part of `path` below its last "convert" folder ("textures/CHARA/HERO.png"), or empty when it has none.
std::filesystem::path below_convert(const std::filesystem::path& path);

// Game bone of a joint name ("bone07" -> 7), or -1.
int bone_of_joint_name(std::string_view name);

// The picture of `tex` read again from its file (texture_upscale: a better one arrived); false leaves it as it was.
bool reload_skin_texture(SkinTexture& tex);

// A model worn on the skeleton of another character than the one it was made for, described by a manifest
// `<NAME>.model.json` without a glb of its own (docs/modding/assets.md, "A model of the import on another
// skeleton"): its "derive" object names the glb below convert/models/ to read ("from"), whether that glb keeps its
// own bone lengths ("proportions": "model"), and which of its meshes are drawn as which parts of the wearer
// ("parts": {"10RHAND": ["10RHAND", "10RHAND2"], "10RGUN": []}; a mesh not named stays under its own name). Its
// meshes are matched to the wearer's parts by name, never by the part numbers of the model they came from. So a mod
// dresses one character in another's imported model without shipping a file of the game.
struct SkinDerive {
    std::string from;  // below convert/models/: "CHARA/SIBYL.glb"
    bool own_proportions = false;
    std::vector<std::pair<std::string, std::vector<std::string>>> parts;
};
// The "derive" object of a manifest; false with `error` set when the file has none or it is malformed.
bool read_skin_derive(const std::filesystem::path& manifest, SkinDerive& out, std::string& error);

// A texture that does not decode is dropped (the material draws untextured) and named in `warnings`. With `derive`,
// the file is read as that description says.
Result<SkinModel> load_skin_model(const std::filesystem::path& file, const TextureResolver& resolve,
                                  std::vector<std::string>& warnings, const SkinDerive* derive = nullptr);

}  // namespace sh::assets
