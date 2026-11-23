#pragma once

#include "formats/common/status.h"
#include "formats/convert/anim/anim_clips.h"
#include "formats/convert/converter.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Model converter: ILM (skeletal character models), TMD (item and test models) and the held items ITEM/*.PLM (LM model
// libraries, parsed by the level module) to glTF 2.0 binary.
//
// decode() of "CHARA/HERO.ILM" returns two files:
//   models/CHARA/HERO.glb          the model: one node per part (ILM) or per object (TMD), one mesh each
//   models/CHARA/HERO.model.json   everything else a modder may need: source offsets, bone indices, rest pose,
//                                  raw primitive bytes, original integer vertices, texture references, counts
//
// The conversion is not lossless (lossless() == false): the game logic keeps reading the raw ILM/TMD; the glTF is a
// representation for rendering and for modders. encode() is not supported.
//
// Coordinates. The PS1 geometry unit is Q8: 256 units are one world unit (the game's Q12 1.0), taken as one metre.
// PS1 axes are +X right, +Y down, +Z forward (into the screen), which is right-handed. The glb is glTF-standard
// (+Y up, a character faces +Z):
//     x' = -x / 256     y' = -y / 256     z' = z / 256
// This is a rotation by 180 degrees about Z, so it keeps the model's handedness (nothing is mirrored; a character's
// right side ends up at -X, which is how Blender shows a glTF character seen from the front). PS1 front faces are
// clockwise on screen, glTF's are counter-clockwise, so the triangle order of every face is reversed. A PS1 quad is
// a strip (v0 v1 v2 v3 drawn as triangles v0 v1 v2 and v1 v2 v3) and becomes those two triangles.
//
// Skeletal ILM parts are stored in the part's own bone frame. When the ANM file that holds the character's skeleton
// can be read (Options::sibling), every part is placed with the pose of keyframe Options::keyframe (default 0, the
// game's own first frame) and the vertices are baked into model space, so the glb looks like the character.
// Without it the parts sit on the origin in their local frames, and model.json says so.
//
// Skin and animations (Options::skin, on by default). With the ANM, the glb also has one joint node per ANM bone
// (named "bone<NN>", parented as in the ANM bone table) and one skin; every part node is skinned and each vertex is
// bound with weight 1 to the bone of the part that owns it in the vertex pool (a seam vertex follows its owner, as
// on the PS1). The inverse bind matrices are the exact inverses of the rest pose above, so the mesh data stays the
// rest pose. Joint rest transforms and animation keys are the game's per-keyframe bone matrices (Anim_BonesUpdate)
// split into T * R * S (skin_math.h). One glTF animation per playback entry of the game's animation table for that
// ANM (anim_clips.json: the clip name, keyframes start..end, keyframes per second); an ANM without a table gets one
// clip "keyframes" over all its keyframes at 30 per second. The disc holds no bind pose: with identity rotations both
// shoulders of HERO fall on one point, so a keyframe of the game is the only coherent rest pose.
//
// Texture coordinates are texel centres: u' = (u + 0.5) / width, v' = (v + 0.5) / height; V grows downward in both
// PS1 and glTF, so there is no flip. The image size comes from the TIM when Options::sibling can read it,
// otherwise 256 x 256 (a full texture page). A material refers to the PNG the texture converter wrote for the palette
// its faces really use: "<TIM path without extension>.png" for palette 0, ".pNN.png" for palette NN, where the
// palette is the face's CLUT word relative to the material's CLUT base (+0x12), counted in the TIM's CLUT block
// (level_textures.h). The URI is relative to the .glb ("../../textures/CHARA/HERO.p03.png" for
// models/CHARA/HERO.glb), as glTF resolves it. Without a readable TIM the palette cannot be counted and the URI names
// palette 0. Opaque textured materials use alphaMode MASK: the PS1 never draws a texel whose word is 0x0000.
namespace sh::convert::model {

// Reads another file of the game by its path ("ANIM/HB_BASE.ANM"). Empty when the file does not exist.
using SiblingReader = std::function<std::optional<std::vector<std::uint8_t>>(std::string_view game_path)>;

struct Options {
    SiblingReader sibling;     // optional; needed for rest poses (ANM) and texture sizes (TIM)
    std::size_t keyframe = 0;  // ANM keyframe used as the rest pose
    bool skin = true;          // ILM with an ANM: joints, a skin and the animation clips (see above)
    const anim::ClipCatalog* clips = nullptr;  // clip names and ranges; nullptr: anim::builtin_clips()
};

std::unique_ptr<sh::convert::Converter> make_converter();
std::unique_ptr<sh::convert::Converter> make_converter(Options options);

// Counts of one converted model, for reports and tests. "Source" numbers come from the ILM/TMD, "glb" numbers from
// what was written.
struct ModelStats {
    std::string kind;  // "ilm", "tmd" or "plm"
    std::size_t parts = 0;              // ILM parts / TMD objects
    std::size_t source_vertices = 0;    // vertices stored in the file (own vertices of every part)
    std::size_t source_triangles = 0;   // triangles after splitting quads
    std::size_t source_primitives = 0;  // triangles + quads as stored
    std::size_t referenced_vertices = 0;  // distinct source vertices that a face reads
    std::size_t part_vertex_refs = 0;     // distinct vertices read by each part's faces, summed over parts
    std::size_t seam_vertices = 0;        // ILM: face corners that read a vertex owned by another part (distinct per part)
    std::size_t glb_vertices = 0;         // sum of POSITION counts in the glb
    std::size_t glb_triangles = 0;
    std::size_t dangling_corners = 0;     // ILM: corners reading a pool slot nobody wrote (replaced by vertex 0)
    bool rest_pose = false;               // ILM: parts placed with an ANM pose
    std::size_t joints = 0;               // ILM: joint nodes of the skin, 0 without one
    std::size_t animations = 0;           // ILM: glTF animations written
    std::string rest_pose_source;         // e.g. "ANIM/HB_BASE.ANM keyframe 0", or why there is none
    std::string texture;                  // TIM path the materials refer to, empty for none
};

struct ConvertedModel {
    std::vector<std::uint8_t> glb;
    std::string sidecar_json;
    ModelStats stats;
};

// The conversion behind Converter::decode, for callers that need the counts. `game_path` selects ILM or TMD by its
// extension.
Result<ConvertedModel> convert_model(std::span<const std::uint8_t> raw, std::string_view game_path,
                                     const Options& options);

}  // namespace sh::convert::model
