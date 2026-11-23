#include "formats/common/crc32.h"
#include "formats/convert/level/level_textures.h"
#include "formats/convert/anim/anim_converter.h"
#include "formats/convert/anim/anm_file.h"
#include "formats/convert/model/ilm.h"
#include "formats/convert/model/model_build.h"
#include "formats/convert/model/skin_math.h"
#include "formats/gltf/gltf_writer.h"
#include "formats/common/json.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <optional>
#include <set>
#include <tuple>

namespace sh::convert::model {
namespace {

// The animation file that holds a character's skeleton, by ILM name (the original's CHARA_FILE_INFOS table).
// First entry wins where a model has several animation sets.
struct AnmName {
    const char* ilm;
    const char* anm;
};
constexpr AnmName kAnmNames[] = {
    {"HERO", "HB_BASE"}, {"BIRD", "BIRD"},   {"BD2", "BIRD"},    {"DOG", "DOG"},     {"DG2", "DOG"},
    {"CLD1", "CLD1"},    {"CLD2", "CLD2"},   {"CLD3", "CLD2"},   {"CLD4", "CLD2"},   {"SLT", "SLT"},
    {"COC", "COC"},      {"JACK", "JACK"},   {"CKN", "CKN"},     {"FAT", "FAT"},     {"MTH", "MTH"},
    {"PRS", "PRS"},      {"DUMMY", "DUMMY"}, {"PRSD", "PRS"},    {"WRM", "WRM"},     {"ROD", "ROD"},
    {"BOS", "BOS"},      {"MAR", "MAR"},     {"MSB", "MSB"},     {"DEAD", "DEAD"},   {"SIBYL", "SBL"},
    {"SRL", "SRL"},      {"CAT", "CAT"},     {"DARIA", "DA"},    {"LISA", "LS"},     {"BLISA", "BLS"},
    {"AR", "AR"},        {"TAR", "TAR"},     {"BAR", "BAR"},     {"KAU", "KAU"},     {"BFLU", "BFLU"},
    {"LITL", "LITL"},    {"DOC", "DOC"},     {"ICU", "ICU"},
};

struct RestPose {
    bool found = false;
    std::vector<anim::Pose> bones;   // model space
    std::optional<anim::AnmFile> anm;
    std::string anm_path;
    std::size_t keyframe = 0;
    std::string source;  // "ANIM/HB_BASE.ANM keyframe 0" or the reason there is none
};

RestPose find_rest_pose(const Ilm& ilm, std::string_view game_path, const Options& options) {
    RestPose out;
    if (!options.sibling) {
        out.source = "no sibling reader: parts are left in their own bone frames";
        return out;
    }
    int max_bone = 0;
    for (const IlmPart& part : ilm.parts) {
        max_bone = std::max(max_bone, part.bone());
    }
    const std::string stem = upper_copy(path_stem(game_path));
    const std::string dir = path_directory(game_path);

    std::vector<std::string> names;
    for (const AnmName& n : kAnmNames) {
        if (stem == n.ilm) {
            names.emplace_back(n.anm);
        }
    }
    names.push_back(stem);

    std::string tried;
    for (const std::string& name : names) {
        for (const std::string& d : {std::string("ANIM"), dir}) {
            const std::string path = (d.empty() ? std::string() : d + "/") + name + ".ANM";
            const auto bytes = options.sibling(path);
            if (!bytes) {
                continue;
            }
            auto skeleton = anim::AnmFile::parse(*bytes);
            if (!skeleton) {
                tried += " " + path + " (" + skeleton.error() + ")";
                continue;
            }
            const std::size_t bone_count = skeleton.value().bones().size();
            if (static_cast<std::size_t>(max_bone) >= bone_count || skeleton.value().keyframes().empty()) {
                tried += " " + path + " (" + std::to_string(bone_count) + " bones, model needs " +
                         std::to_string(max_bone + 1) + ")";
                continue;
            }
            std::size_t keyframe = options.keyframe;
            if (keyframe >= skeleton.value().keyframes().size()) {
                keyframe = 0;
            }
            out.bones = skeleton.value().world_poses(skeleton.value().local_poses(keyframe, keyframe, 0));
            out.found = true;
            out.source = path + " keyframe " + std::to_string(keyframe);
            out.anm = std::move(skeleton.value());
            out.anm_path = path;
            out.keyframe = keyframe;
            return out;
        }
    }
    out.source = "no usable ANM found" + (tried.empty() ? std::string() : ":" + tried);
    return out;
}

struct TextureRef {
    std::string tim_path;  // empty: the material has no texture
    std::string uri;       // palette 0, relative to the .glb
    int width = 256;
    int height = 256;
    bool size_known = false;
    level::TimInfo tim;    // header facts; palette_count 0 when the TIM could not be read
};

TextureRef find_texture(const std::string& material_name, std::string_view game_path, const Options& options,
                        const std::string& glb_path) {
    TextureRef ref;
    if (material_name.empty()) {
        return ref;
    }
    const std::string dir = path_directory(game_path);
    const auto join = [&](const std::string& d, const std::string& n) { return (d.empty() ? "" : d + "/") + n + ".TIM"; };
    // The two unused debug characters CHARA/EI.ILM and CHARA/MAN.ILM have their sheets (EI.TIM, DEV.TIM) only in TEST/.
    const std::vector<std::string> candidates = {join(dir, material_name), join("CHARA", material_name),
                                                 join(dir, upper_copy(path_stem(game_path))), join("TEST", material_name)};
    if (options.sibling) {
        for (const std::string& c : candidates) {
            const auto bytes = options.sibling(c);
            if (!bytes) {
                continue;
            }
            ref.tim_path = c;
            if (auto info = level::read_tim_info(*bytes, c)) {
                ref.tim = std::move(info.value());
                ref.width = ref.tim.width;
                ref.height = ref.tim.height;
                ref.size_known = true;
            }
            break;
        }
    }
    if (ref.tim_path.empty()) {
        ref.tim_path = candidates[0];
    }
    ref.tim.game_path = ref.tim_path;
    ref.uri = level::relative_uri(glb_path, level::palette_png(ref.tim, 0));
    return ref;
}

struct MaterialKey {
    int index;
    int clut;
    bool semi;
    bool operator<(const MaterialKey& o) const {
        return std::tie(index, clut, semi) < std::tie(o.index, o.clut, o.semi);
    }
};

struct Group {
    gltf::Primitive prim;
    std::map<std::array<int, 8>, std::uint32_t> lookup;
};

using GlobalRef = std::array<int, 3>;

// Joint nodes, the skin and the animation clips of a skinned ILM (model_converter.h, "Skin and animations").
struct SkinResult {
    std::size_t joints = 0;
    int joint_base = -1;  // node index of bone 0
    std::vector<std::string> clips;
    std::vector<std::string> skipped;  // clips whose keyframes the ANM does not have
    bool named = false;                // the clips came from an animation table
    std::vector<std::string> packed;   // ANM files packed into the glb (kAnimFilesExtension)
};

// Root extension of a character glb that carries the game's own animation files byte for byte, so the engine can take
// the exact keyframes from the model it draws (the glTF animations are a float decomposition of them).
constexpr std::string_view kAnimFilesExtension = "SH_anim_files";
constexpr int kAnimFilesVersion = 1;

skin::Trs local_trs(const anim::Pose& p) { return skin::to_trs(skin::to_gltf(p)); }

SkinResult add_skin(gltf::Document& doc, const RestPose& rest, const std::vector<int>& part_nodes,
                    const Options& options) {
    SkinResult result;
    const anim::AnmFile& anm = *rest.anm;
    const std::vector<anim::AnmBone>& bones = anm.bones();
    const std::size_t n = bones.size();
    result.joints = n;
    result.joint_base = static_cast<int>(doc.nodes.size());

    const std::vector<anim::Pose> rest_local = anm.local_poses(rest.keyframe, rest.keyframe, 0);
    for (std::size_t b = 0; b < n; ++b) {
        gltf::Node joint;
        char name[16];
        std::snprintf(name, sizeof name, "bone%02u", static_cast<unsigned>(b & 0xFF));
        joint.name = name;
        const skin::Trs trs = local_trs(rest_local[b]);
        joint.translation = trs.translation;
        joint.rotation = trs.rotation;
        joint.scale = trs.scale;
        json::Writer e;
        e.begin_object();
        e.member("bone", b);
        e.member("parent", bones[b].parent);
        e.member("rotation_slot", bones[b].rotation);
        e.member("translation_slot", bones[b].translation);
        e.end_object();
        joint.extras = e.take();
        doc.nodes.push_back(std::move(joint));
    }
    for (std::size_t b = 0; b < n; ++b) {
        const int parent = bones[b].parent;
        const bool has_parent = parent >= 0 && static_cast<std::size_t>(parent) < n && static_cast<std::size_t>(parent) != b;
        // A parent cycle would make the node tree invalid; such a bone hangs from the model root instead.
        bool cycle = false;
        if (has_parent) {
            std::size_t steps = 0;
            for (int cur = parent; cur >= 0 && static_cast<std::size_t>(cur) < n; cur = bones[static_cast<std::size_t>(cur)].parent) {
                if (static_cast<std::size_t>(cur) == b || ++steps > n) {
                    cycle = true;
                    break;
                }
            }
        }
        const int node = result.joint_base + static_cast<int>(b);
        if (has_parent && !cycle) {
            doc.nodes[static_cast<std::size_t>(result.joint_base + parent)].children.push_back(node);
        } else {
            doc.nodes[0].children.push_back(node);
        }
    }

    gltf::Skin sk;
    sk.name = doc.nodes[0].name;
    sk.skeleton = result.joint_base;
    for (std::size_t b = 0; b < n; ++b) {
        sk.joints.push_back(result.joint_base + static_cast<int>(b));
        sk.inverse_bind.push_back(skin::inverse_column_major(skin::to_gltf(rest.bones[b])));
    }
    doc.skins.push_back(std::move(sk));
    for (const int node : part_nodes) {
        if (doc.nodes[static_cast<std::size_t>(node)].mesh >= 0) {
            doc.nodes[static_cast<std::size_t>(node)].skin = 0;
        }
    }

    const anim::ClipCatalog& catalog = options.clips ? *options.clips : anim::builtin_clips();
    std::vector<anim::Clip> clips = anim::clips_for(catalog, rest.anm_path);
    result.named = !clips.empty();
    if (clips.empty()) {
        anim::Clip all;
        all.name = "keyframes";
        all.start = 0;
        all.end = static_cast<int>(anm.keyframes().size()) - 1;
        all.fps = 30.0;
        clips.push_back(all);
    }
    // One glTF animation over keyframes start..end of `file` (the base ANM or a keyframe file in its layout).
    const auto add_clip = [&](const anim::AnmFile& file, const anim::Clip& clip, const std::string& file_path,
                              int first_keyframe_index) {
        if (clip.end >= static_cast<int>(file.keyframes().size())) {
            result.skipped.push_back(clip.name);
            return;
        }
        const double fps = clip.fps > 0 ? clip.fps : 30.0;
        const std::size_t keys = static_cast<std::size_t>(clip.end - clip.start + 1);
        gltf::Animation an;
        an.name = clip.name;
        std::vector<float> times(keys);
        for (std::size_t k = 0; k < keys; ++k) {
            times[k] = static_cast<float>(static_cast<double>(k) / fps);
        }
        std::vector<std::vector<skin::Trs>> per_key(keys);
        for (std::size_t k = 0; k < keys; ++k) {
            const auto local = file.local_poses(static_cast<std::size_t>(clip.start) + k,
                                                static_cast<std::size_t>(clip.start) + k, 0);
            per_key[k].reserve(n);
            for (std::size_t b = 0; b < n; ++b) {
                per_key[k].push_back(local_trs(local[b]));
            }
        }
        for (std::size_t b = 1; b < n; ++b) {
            const int node = result.joint_base + static_cast<int>(b);
            if (bones[b].translation >= 0) {
                gltf::AnimationSampler sm;
                sm.input = times;
                for (std::size_t k = 0; k < keys; ++k) {
                    sm.output.insert(sm.output.end(), per_key[k][b].translation.begin(), per_key[k][b].translation.end());
                }
                an.samplers.push_back(std::move(sm));
                an.channels.push_back({static_cast<int>(an.samplers.size() - 1), node, gltf::AnimationPath::Translation});
            }
            if (bones[b].rotation >= 0) {
                gltf::AnimationSampler rot, scale;
                rot.input = times;
                scale.input = times;
                std::array<float, 4> prev{0, 0, 0, 1};
                for (std::size_t k = 0; k < keys; ++k) {
                    std::array<float, 4> q = per_key[k][b].rotation;
                    // Keep neighbouring keys in one hemisphere so LINEAR (slerp) takes the short way.
                    if (k > 0 && q[0] * prev[0] + q[1] * prev[1] + q[2] * prev[2] + q[3] * prev[3] < 0) {
                        for (float& c : q) {
                            c = -c;
                        }
                    }
                    prev = q;
                    rot.output.insert(rot.output.end(), q.begin(), q.end());
                    scale.output.insert(scale.output.end(), per_key[k][b].scale.begin(), per_key[k][b].scale.end());
                }
                an.samplers.push_back(std::move(rot));
                an.channels.push_back({static_cast<int>(an.samplers.size() - 1), node, gltf::AnimationPath::Rotation});
                an.samplers.push_back(std::move(scale));
                an.channels.push_back({static_cast<int>(an.samplers.size() - 1), node, gltf::AnimationPath::Scale});
            }
        }
        if (an.channels.empty()) {
            result.skipped.push_back(clip.name);
            return;
        }
        json::Writer e;
        e.begin_object();
        e.member("anm", file_path);
        if (first_keyframe_index > 0) {
            // A keyframe file sits behind the base file's keyframes: the game numbers its keyframes from here.
            e.member("first_keyframe_index", first_keyframe_index);
        }
        e.member("start_keyframe", clip.start);
        e.member("end_keyframe", clip.end);
        e.member_double("keyframes_per_second", fps);
        e.member("loop", clip.loop);
        e.member("variable_speed", clip.variable_speed);
        e.member("reverse", clip.reverse);
        e.end_object();
        an.extras = e.take();
        result.clips.push_back(an.name);
        doc.animations.push_back(std::move(an));
    };
    for (const anim::Clip& clip : clips) {
        add_clip(anm, clip, rest.anm_path, 0);
    }

    // Harry's map files: keyframes in the base file's layout that the game loads behind it per map. Their clips are
    // rows of each map's own table, so every file is one animation over all its keyframes, named after the file; the
    // file bytes go into the glb with the base file's.
    if (rest.anm_path == anim::kHarryBaseAnm && options.sibling) {
        std::vector<std::pair<std::string, std::vector<std::uint8_t>>> files;
        if (auto base = options.sibling(rest.anm_path)) {
            files.emplace_back(rest.anm_path, std::move(*base));
        }
        for (const std::string_view path : anim::harry_map_keyframe_files()) {
            auto bytes = options.sibling(path);
            if (!bytes || bytes->empty()) {
                continue;
            }
            auto ext = anim::AnmFile::parse_extension(*bytes, anm);
            if (!ext || ext.value().keyframes().empty()) {
                result.skipped.push_back(std::string(path));
                continue;
            }
            anim::Clip clip;
            clip.name = path_stem(path);
            clip.start = 0;
            clip.end = static_cast<int>(ext.value().keyframes().size()) - 1;
            clip.fps = 30.0;
            add_clip(ext.value(), clip, std::string(path), static_cast<int>(anm.keyframes().size()));
            files.emplace_back(std::string(path), std::move(*bytes));
        }
        if (!files.empty()) {
            json::Writer ext;
            ext.begin_object();
            ext.key(kAnimFilesExtension).begin_object();
            ext.member("version", kAnimFilesVersion);
            ext.member("base", rest.anm_path);
            ext.key("files").begin_array();
            for (auto& [path, bytes] : files) {
                ext.begin_object();
                ext.member("path", path);
                ext.member("kind", path == rest.anm_path ? "anm" : "keyframes");
                ext.member("size", static_cast<std::uint64_t>(bytes.size()));
                ext.member("crc32", crc32(bytes.data(), bytes.size()));
                ext.member("bufferView", doc.blobs.size());
                ext.end_object();
                result.packed.push_back(path);
                doc.blobs.push_back(std::move(bytes));
            }
            ext.end_array();
            ext.end_object();
            ext.end_object();
            doc.extensions = ext.take();
            doc.extensions_used.push_back(std::string(kAnimFilesExtension));
        }
    }
    return result;
}

}  // namespace

Result<ConvertedModel> build_ilm(std::span<const std::uint8_t> raw, std::string_view game_path,
                                 const Options& options) {
    using Out = Result<ConvertedModel>;
    auto parsed = parse_ilm(raw);
    if (!parsed) {
        return Out::fail(parsed.error());
    }
    const Ilm& ilm = parsed.value();
    const ResolvedIlm resolved = resolve_pool(ilm);
    const RestPose rest = find_rest_pose(ilm, game_path, options);
    const bool skinned = rest.found && rest.anm && options.skin;

    std::vector<anim::Pose> part_pose(ilm.parts.size());
    if (rest.found) {
        for (std::size_t i = 0; i < ilm.parts.size(); ++i) {
            part_pose[i] = rest.bones[static_cast<std::size_t>(ilm.parts[i].bone())];
        }
    }

    const std::string glb_path = "models/" + path_without_extension(game_path) + ".glb";
    std::vector<TextureRef> textures;
    for (const IlmMaterial& m : ilm.materials) {
        textures.push_back(find_texture(m.name, game_path, options, glb_path));
    }

    gltf::Document doc;
    {
        json::Writer e;
        e.begin_object().member("source", std::string(game_path)).end_object();
        doc.extras = e.take();
    }
    std::map<std::string, int> texture_index;
    std::map<MaterialKey, int> material_index;

    const auto material_for = [&](const IlmPrimitive& prim) {
        const MaterialKey key{prim.material(), prim.clut, prim.semi_transparent()};
        const auto it = material_index.find(key);
        if (it != material_index.end()) {
            return it->second;
        }
        gltf::Material mat;
        const int row = prim.clut >> 6;
        char name[48];
        std::snprintf(name, sizeof name, "mat%02d_row%02d%s", key.index, row, key.semi ? "_alpha" : "");
        mat.name = name;
        const bool have_material = key.index >= 0 && static_cast<std::size_t>(key.index) < ilm.materials.size();
        json::Writer extras;
        extras.begin_object();
        extras.member("material_index", key.index);
        extras.member("clut", prim.clut);
        extras.member("clut_x", (prim.clut & 63) * 16);
        extras.member("clut_row", row);
        extras.member("semi_transparent", key.semi);
        if (key.semi) {
            // The ILM's page byte is zero on the disc: the game gives the mode when it binds the texture, from the
            // character type's table row (one model file serves types with different modes: MAR is additive for
            // the Incubator and average for Unknown 23), and map scripts change it at run time.
            extras.member("blend_mode", "character");
        }
        if (have_material) {
            const TextureRef& t = textures[static_cast<std::size_t>(key.index)];
            extras.member("source_material", ilm.materials[static_cast<std::size_t>(key.index)].name);
            if (!t.tim_path.empty()) {
                // The game rebases every face's CLUT on the material's CLUT base at +0x12 (Model_MaterialFlagsApply).
                const std::uint16_t base = static_cast<std::uint16_t>(
                    ilm.materials[static_cast<std::size_t>(key.index)].raw[0x12] |
                    (ilm.materials[static_cast<std::size_t>(key.index)].raw[0x13] << 8));
                const int palette = level::palette_index(t.tim, prim.clut, base);
                const std::string png = level::palette_png(t.tim, std::max(palette, 0));
                const std::string uri = level::relative_uri(glb_path, png);
                extras.member("tim", t.tim_path);
                extras.member("tim_width", t.width);
                extras.member("tim_height", t.height);
                extras.member("clut_base", base);
                extras.member("palette", std::max(palette, 0));
                extras.member("palette_in_tim", palette >= 0);
                extras.member("png", png);
                auto it2 = texture_index.find(uri);
                if (it2 == texture_index.end()) {
                    doc.textures.push_back({uri, true});
                    it2 = texture_index.emplace(uri, static_cast<int>(doc.textures.size() - 1)).first;
                }
                mat.texture = it2->second;
                mat.alpha_mode = gltf::AlphaMode::Mask;
            }
        }
        extras.end_object();
        mat.extras = extras.take();
        if (key.semi) {
            mat.alpha_mode = gltf::AlphaMode::Blend;
            mat.base_color[3] = 0.5f;
        }
        doc.materials.push_back(std::move(mat));
        return material_index.emplace(key, static_cast<int>(doc.materials.size() - 1)).first->second;
    };

    const auto local_vertex = [&](const PoolRef& r) -> std::array<double, 3> {
        const auto& v = ilm.parts[static_cast<std::size_t>(r.part)].meshes[static_cast<std::size_t>(r.mesh)]
                            .vertices[static_cast<std::size_t>(r.index)];
        return {double(v[0]), double(v[1]), double(v[2])};
    };
    const auto baked_position = [&](const PoolRef& r) -> std::array<float, 3> {
        const anim::Pose& pose = part_pose[static_cast<std::size_t>(r.part)];
        const auto v = local_vertex(r);
        double w[3];
        for (std::size_t a = 0; a < 3; ++a) {
            w[a] = (pose.m[a * 3] * v[0] + pose.m[a * 3 + 1] * v[1] + pose.m[a * 3 + 2] * v[2]) /
                       4096.0 +
                   pose.t[a];
        }
        return to_gltf_position(w[0], w[1], w[2]);
    };
    // ILM normals point into the body (93.5% of faces of the first 20 CHARA models), so the outward
    // normal is the negated one.
    const auto baked_normal = [&](const PoolRef& r) -> std::array<float, 3> {
        const anim::Pose& pose = part_pose[static_cast<std::size_t>(r.part)];
        const IlmNormal& n =
            ilm.parts[static_cast<std::size_t>(r.part)].meshes[static_cast<std::size_t>(r.mesh)]
                .normals[static_cast<std::size_t>(r.index)];
        const double v[3] = {n.x / 128.0, n.y / 128.0, n.z / 128.0};
        double w[3];
        for (std::size_t a = 0; a < 3; ++a) {
            w[a] = (pose.m[a * 3] * v[0] + pose.m[a * 3 + 1] * v[1] + pose.m[a * 3 + 2] * v[2]) /
                   4096.0;
        }
        return to_gltf_direction(-w[0], -w[1], -w[2]);
    };

    ModelStats stats;
    stats.kind = "ilm";
    stats.parts = ilm.parts.size();
    stats.rest_pose = rest.found;
    stats.rest_pose_source = rest.source;
    stats.dangling_corners = resolved.dangling_vertex_corners + resolved.dangling_normal_corners;
    for (const TextureRef& t : textures) {
        if (!t.tim_path.empty()) {
            stats.texture = t.tim_path;
            break;
        }
    }

    // Node 0 is the model; node 1 + i is part i.
    gltf::Node root;
    root.name = path_stem(game_path);
    doc.nodes.push_back(root);

    std::set<GlobalRef> referenced;
    std::vector<int> part_nodes;
    json::Writer side(true);
    side.begin_object();
    side.member("format", "sh-model/1");
    side.key("source").begin_object();
    side.member("path", std::string(game_path));
    side.member("kind", "ilm");
    side.member("size", raw.size());
    char crc[16];
    std::snprintf(crc, sizeof crc, "%08X", crc32(raw.data(), raw.size()));
    side.member("crc32", crc);
    side.member("lossless", false);
    side.end_object();

    side.key("coordinates").begin_object();
    side.member("units_per_metre", kUnitsPerMetre);
    side.member("transform", "x' = -x/256, y' = -y/256, z' = z/256 (rotation by 180 degrees about Z)");
    side.member("winding", "triangle corner order reversed (PS1 front faces are clockwise)");
    side.member("quad", "strip: triangles (0,1,2) and (1,2,3) of the PS1 corner order");
    side.member("uv", "(u + 0.5) / tim_width, (v + 0.5) / tim_height; V down, no flip");
    side.member("normals", "source normals negated (they point into the body), rotated by the part pose");
    side.end_object();

    side.key("rest_pose").begin_object();
    side.member("applied", rest.found);
    side.member("source", rest.source);
    side.member("keyframe", options.keyframe);
    side.end_object();

    side.key("materials").begin_array();
    for (std::size_t i = 0; i < ilm.materials.size(); ++i) {
        side.begin_object();
        side.member("index", i);
        side.member("name", ilm.materials[i].name);
        side.member("offset", ilm.materials[i].offset);
        side.member("raw", hex_bytes(ilm.materials[i].raw));
        side.member("tim", textures[i].tim_path);
        side.member("texture", textures[i].uri);
        side.member("tim_size_known", textures[i].size_known);
        side.member("width", textures[i].width);
        side.member("height", textures[i].height);
        side.end_object();
    }
    side.end_array();

    side.key("draw_order").begin_array();
    for (const std::uint8_t o : ilm.order) {
        side.value(o);
    }
    side.end_array();
    side.member("draw_order_is_permutation", ilm.order_is_permutation);

    side.key("parts").begin_array();
    for (std::size_t pi = 0; pi < ilm.parts.size(); ++pi) {
        const IlmPart& part = ilm.parts[pi];
        std::map<int, Group> groups;  // by glTF material index; the map order makes the output deterministic
        std::set<GlobalRef> seam;
        std::set<GlobalRef> part_refs;
        std::size_t part_triangles = 0;

        side.begin_object();
        side.member("index", pi);
        side.member("name", part.name);
        side.member("offset", part.offset);
        side.member("bone", part.bone());
        side.member("vertex_offset", part.vertex_offset);
        side.member("normal_offset", part.normal_offset);
        side.member("bits", part.bits);
        {
            const anim::Pose& pose = part_pose[pi];
            json::Writer c;
            c.begin_object().key("rotation_q12").begin_array();
            for (const std::int32_t v : pose.m) {
                c.value(v);
            }
            c.end_array().key("translation").begin_array();
            for (const std::int32_t v : pose.t) {
                c.value(v);
            }
            c.end_array().end_object();
            side.key("pose").raw(c.str());
        }

        side.key("meshes").begin_array();
        for (std::size_t mi = 0; mi < part.meshes.size(); ++mi) {
            const IlmMesh& mesh = part.meshes[mi];
            side.begin_object();
            side.member("offset", mesh.offset);
            side.member("primitives_offset", mesh.prims_offset);
            side.member("vertices_xy_offset", mesh.xy_offset);
            side.member("vertices_z_offset", mesh.z_offset);
            side.member("normals_offset", mesh.normals_offset);
            side.member("unknown_offset", mesh.unknown_offset);
            side.member("unknown_count", mesh.unknown_count);
            {
                json::Writer c;
                c.begin_array();
                for (const auto& v : mesh.vertices) {
                    c.begin_array().value(v[0]).value(v[1]).value(v[2]).end_array();
                }
                c.end_array();
                side.key("vertices").raw(c.str());
            }
            {
                json::Writer c;
                c.begin_array();
                for (const IlmNormal& n : mesh.normals) {
                    c.begin_array().value(n.x).value(n.y).value(n.z).value(n.count).end_array();
                }
                c.end_array();
                side.key("normals").raw(c.str());
            }

            side.key("primitives").begin_array();
            for (std::size_t i = 0; i < mesh.prims.size(); ++i) {
                const IlmPrimitive& prim = mesh.prims[i];
                const ResolvedPrim& rp = resolved.prims[pi][mi][i];
                const int material = material_for(prim);
                Group& group = groups[material];
                group.prim.material = material;

                std::array<std::uint32_t, 4> ids{};
                for (int k = 0; k < prim.corners(); ++k) {
                    const auto ku = static_cast<std::size_t>(k);
                    PoolRef vr = rp.vertex[ku];
                    PoolRef nr = rp.normal[ku];
                    // A corner that reads a pool slot nobody wrote falls back to vertex 0 / normal 0 of its own part.
                    if (!vr.valid()) {
                        vr = {static_cast<int>(pi), static_cast<int>(mi), 0};
                    }
                    if (!nr.valid()) {
                        nr = {static_cast<int>(pi), static_cast<int>(mi), 0};
                    }
                    const bool vr_ok = static_cast<std::size_t>(vr.index) < ilm.parts[static_cast<std::size_t>(vr.part)]
                                                                               .meshes[static_cast<std::size_t>(vr.mesh)]
                                                                               .vertices.size();
                    const bool nr_ok = static_cast<std::size_t>(nr.index) < ilm.parts[static_cast<std::size_t>(nr.part)]
                                                                               .meshes[static_cast<std::size_t>(nr.mesh)]
                                                                               .normals.size();
                    const std::array<int, 8> key = {vr.part, vr.mesh, vr.index, nr.part,
                                                    nr.mesh, nr.index, prim.uv[ku][0], prim.uv[ku][1]};
                    const auto found = group.lookup.find(key);
                    if (found != group.lookup.end()) {
                        ids[ku] = found->second;
                    } else {
                        gltf::Primitive& gp = group.prim;
                        const auto id = static_cast<std::uint32_t>(gp.vertex_count());
                        const std::array<float, 3> pos = vr_ok ? baked_position(vr) : std::array<float, 3>{0, 0, 0};
                        const std::array<float, 3> nrm = nr_ok ? baked_normal(nr) : std::array<float, 3>{0, 1, 0};
                        gp.positions.insert(gp.positions.end(), pos.begin(), pos.end());
                        gp.normals.insert(gp.normals.end(), nrm.begin(), nrm.end());
                        const bool have_tex = prim.material() >= 0 &&
                                              static_cast<std::size_t>(prim.material()) < textures.size();
                        const TextureRef fallback;
                        const TextureRef& t = have_tex ? textures[static_cast<std::size_t>(prim.material())] : fallback;
                        gp.texcoords.push_back(static_cast<float>((prim.uv[ku][0] + 0.5) / t.width));
                        gp.texcoords.push_back(static_cast<float>((prim.uv[ku][1] + 0.5) / t.height));
                        if (skinned) {
                            // The PS1 transforms a pool vertex with the bone of the part that wrote it.
                            const auto bone = static_cast<std::uint16_t>(ilm.parts[static_cast<std::size_t>(vr.part)].bone());
                            gp.joints.insert(gp.joints.end(), {bone, 0, 0, 0});
                            gp.weights.insert(gp.weights.end(), {1.0f, 0.0f, 0.0f, 0.0f});
                        }
                        group.lookup.emplace(key, id);
                        ids[ku] = id;
                    }
                    if (vr_ok) {
                        referenced.insert({vr.part, vr.mesh, vr.index});
                        part_refs.insert({vr.part, vr.mesh, vr.index});
                        if (vr.part != static_cast<int>(pi)) {
                            seam.insert({vr.part, vr.mesh, vr.index});
                        }
                    }
                }
                const auto tris = face_triangles(!prim.triangle);
                for (std::size_t t = 0; t < (prim.triangle ? 1u : 2u); ++t) {
                    for (const int corner : tris[t]) {
                        group.prim.indices.push_back(ids[static_cast<std::size_t>(corner)]);
                    }
                    ++part_triangles;
                }
                stats.source_primitives += 1;
                stats.source_triangles += prim.triangle ? 1 : 2;

                json::Writer c;
                c.begin_object();
                c.member("offset", prim.offset);
                c.member("quad", !prim.triangle);
                c.member("gltf_material", material);
                c.member("raw", hex_bytes(prim.raw));
                c.end_object();
                side.raw(c.str());
            }
            side.end_array();
            side.end_object();
            stats.source_vertices += mesh.vertices.size();
        }
        side.end_array();

        gltf::Node node;
        node.name = part.name;
        {
            json::Writer e;
            e.begin_object();
            e.member("part_index", pi);
            e.member("bone", part.bone());
            e.member("source_offset", part.offset);
            e.end_object();
            node.extras = e.take();
        }
        if (!groups.empty()) {
            gltf::Mesh mesh;
            mesh.name = part.name;
            for (auto& entry : groups) {
                stats.glb_vertices += entry.second.prim.vertex_count();
                stats.glb_triangles += entry.second.prim.indices.size() / 3;
                mesh.primitives.push_back(std::move(entry.second.prim));
            }
            doc.meshes.push_back(std::move(mesh));
            node.mesh = static_cast<int>(doc.meshes.size() - 1);
        }
        doc.nodes.push_back(std::move(node));
        doc.nodes[0].children.push_back(static_cast<int>(doc.nodes.size() - 1));
        part_nodes.push_back(static_cast<int>(doc.nodes.size() - 1));

        side.member("triangles", part_triangles);
        side.member("seam_vertices", seam.size());
        side.member("node", doc.nodes.size() - 1);
        side.end_object();
        stats.seam_vertices += seam.size();
        stats.part_vertex_refs += part_refs.size();
    }
    side.end_array();

    stats.referenced_vertices = referenced.size();
    if (skinned) {
        const SkinResult sr = add_skin(doc, rest, part_nodes, options);
        stats.joints = sr.joints;
        stats.animations = sr.clips.size();
        side.key("skin").begin_object();
        side.member("anm", rest.anm_path);
        side.member("bind_keyframe", rest.keyframe);
        side.member("joints", sr.joints);
        side.member("first_joint_node", sr.joint_base);
        side.member("weights", "rigid: each vertex weight 1 on the bone of the part that owns it in the pool");
        side.member("inverse_bind", "exact inverse of the rest pose in model space");
        side.member("joint_transform",
                    "game bone matrix (Anim_BonesUpdate, Q12) split as T * R * S by polar decomposition");
        side.member("clip_source", sr.named ? "anim_clips.json (the game's animation table)"
                                            : "no animation table: one clip over all keyframes at 30 per second");
        side.key("clips").begin_array();
        for (const std::string& c : sr.clips) {
            side.value(c);
        }
        side.end_array();
        side.key("skipped_clips").begin_array();
        for (const std::string& c : sr.skipped) {
            side.value(c);
        }
        side.end_array();
        if (!sr.packed.empty()) {
            side.member("packed_extension", std::string(kAnimFilesExtension));
            side.key("packed_anm").begin_array();
            for (const std::string& c : sr.packed) {
                side.value(c);
            }
            side.end_array();
        }
        side.end_object();
    }
    side.key("totals").begin_object();
    side.member("parts", stats.parts);
    side.member("source_vertices", stats.source_vertices);
    side.member("source_primitives", stats.source_primitives);
    side.member("source_triangles", stats.source_triangles);
    side.member("referenced_vertices", stats.referenced_vertices);
    side.member("seam_vertices", stats.seam_vertices);
    side.member("glb_vertices", stats.glb_vertices);
    side.member("dangling_corners", stats.dangling_corners);
    side.end_object();
    side.end_object();

    doc.scene_name = path_stem(game_path);
    auto glb = gltf::write_glb(doc);
    if (!glb) {
        return Out::fail(glb.error());
    }
    ConvertedModel result;
    result.glb = std::move(glb.value());
    result.sidecar_json = side.take() + "\n";
    result.stats = std::move(stats);
    return Out::ok(std::move(result));
}

}  // namespace sh::convert::model
