// SPDX-License-Identifier: GPL-3.0-or-later
// Characters from their skinned glb for the engine's scene renderer -- see gltf_chara.h.
//
// Load (once per file): engine/assets/skin_model.h gives every vertex as up to four positions local to game bones, in
// game units, so a pose is the game's own W_bone * v. Each mesh group's vertices are gathered once.
//
// Frame: every part the skeleton shows brings its bone's matrices (world and view, from the game's frames); bones a
// vertex needs that no visible part brought are read from the frames afterwards. With smooth_models a bone is seen
// through the frame's unrounded camera (the one the map is drawn with), from the double twin of its world transform
// (render/precision) or, where that fails, from its integer world matrix. Light is the engine's per-bone light
// (engine/world/bone_light.h) followed by the single-normal colour operation on the vertex normal, in the space of
// the vertex's dominant bone. Output is one view-space mesh per material for the scene, linked into the ordering
// table where the character's parts would have gone, so the effects around it keep their order.

#include "render/skinning/gltf_chara.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "engine/assets/skin_model.h"
#include "engine/world/bone_light.h"
#include "engine/vfs/vfs_c.h"
#include "engine/world/world_atmosphere.h"
#include "render/frame/frame.h"
#include "engine/camera/view_system.h"
#include "engine/math/precise_capture.h"
#include "render/game_view.h"
#include "render/precision/pc_smooth_xform.h"
#include "render/scene/model_upscale.h"
#include "render/scene/scene.h"
#include "render/scene/scene_c.h"

namespace sh::render {
namespace {

namespace fs = std::filesystem;

constexpr int kMaxModels = 64;
constexpr int kMaxBones = 64;
constexpr int kMaxParts = 64;
// The ordering-table base of the characters' parts.
constexpr int kOtBase = 2;

// The triangles of one mesh node with one material over the vertices they use (`verts` in first-use order,
// `tris` local to it).
struct Group {
    int material = -1;
    int part = -1;  // the disc model's part the node is named after, -1: always drawn
    std::vector<int> verts;
    std::vector<std::uint32_t> tris;
};

struct Model {
    std::string key;  // "CHARA/HERO.glb"
    std::unique_ptr<sh::assets::SkinModel> skin;  // null: missing or unusable, not looked for again
    std::vector<ShortVec4> normal;                // per vertex: the inward normal, Q12 (the light's convention)
    std::vector<std::uint8_t> dominant;
    std::vector<Group> groups;
    // Model upscaling (render/scene/model_upscale.h): vertices welded by their skin influences, which place them
    // alike in every pose.
    sh::scene::PnWeld weld;
    std::vector<std::uint32_t> triangles;  // every group's triangles, model vertex indices
    // A mod's file: drawn as the mod made it, never upscaled (an HD model has its own shape).
    bool from_mod = false;
    const void* parts_of = nullptr;  // the model the groups' parts were matched against
    bool has_solid_material = false;  // a material the file does not mark semi-transparent
};

struct Instance {
    Model* model = nullptr;
    const void* parts_model = nullptr;
    void* frames = nullptr;
    int bone_limit = 0;
    int light_mode = 0;
    ModelBlendState blend;
    std::array<std::uint8_t, kMaxBones> have{};
    std::array<std::uint8_t, kMaxBones> lit{};
    std::array<std::uint8_t, kMaxParts> part_visible{};
    std::array<sh::assets::BoneTransform, kMaxBones> bones{};
    std::array<BoneLight, kMaxBones> light{};
    std::array<std::int16_t, 9> color{};  // the light colour matrix the last lit bone left
    // A model with its own proportions: how far every bone's joint is from where the game's skeleton has it, in
    // world units (the bones turn as the game turns them; only their places differ).
    bool shifted = false;
    std::array<std::array<double, 3>, kMaxBones> shift{};
};

// The scene's key of a model's texture: the model's address and the texture's index.
std::uint64_t texture_key(const Model& m, int texture) {
    return (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&m)) << 8) |
           static_cast<std::uint64_t>(texture + 1);
}

std::vector<std::unique_ptr<Model>>& models() {
    static std::vector<std::unique_ptr<Model>> list;
    return list;
}

Instance& instance() {
    static Instance in;
    return in;
}

// Scratch of one character's pose, grown to the largest model.
struct PoseScratch {
    std::vector<float> xyz;
    std::vector<std::uint8_t> ok;
    std::vector<std::uint8_t> rgb;
};
PoseScratch& scratch() {
    static PoseScratch s;
    return s;
}

void warn_once(const std::string& key, const std::string& message) {
    static std::set<std::string> said;
    if (said.insert(key).second) {
        std::fprintf(stderr, "warning: [MODEL] %s\n", message.c_str());
    }
}

// Images below convert/ are looked up in the mods first, like the file itself.
fs::path resolve_converted(const fs::path& resolved) {
    const fs::path rel = sh::assets::below_convert(resolved);
    if (rel.empty()) {
        return resolved;
    }
    char host[1024];
    return ShVfs_FindConverted(rel.generic_string().c_str(), host, sizeof host) ? fs::path(host) : resolved;
}

std::unique_ptr<sh::assets::SkinModel> load(const std::string& key, std::string& host_path, bool& from_mod) {
    const std::string rel = "models/" + key;
    char host[1024];
    sh::assets::SkinDerive derive;
    bool derived = false;
    if (!ShVfs_FindConverted(rel.c_str(), host, sizeof host)) {
        // No glb: a manifest that dresses the character in another model of the import (skin_model.h SkinDerive).
        const std::size_t dot = rel.rfind('.');
        const std::string manifest = rel.substr(0, dot) + ".model.json";
        if (dot == std::string::npos || !ShVfs_FindConverted(manifest.c_str(), host, sizeof host)) {
            return nullptr;
        }
        std::string error;
        if (!sh::assets::read_skin_derive(host, derive, error)) {
            warn_once(host, std::string(host) + ": " + error + "; the character is drawn from its disc model");
            return nullptr;
        }
        // The mesh is the import's own, so model upscaling treats it like the game's.
        from_mod = false;
        const std::string from = "models/" + derive.from;
        if (!ShVfs_FindConverted(from.c_str(), host, sizeof host)) {
            warn_once(manifest, manifest + ": the model it derives from, " + from + ", is not there");
            return nullptr;
        }
        derived = true;
    }
    host_path = host;
    if (!derived) {
        from_mod = ShVfs_FindModConverted(rel.c_str(), host, sizeof host) != 0;
        std::memcpy(host, host_path.c_str(), host_path.size() + 1);
    }
    std::vector<std::string> warnings;
    auto loaded = sh::assets::load_skin_model(host, resolve_converted, warnings, derived ? &derive : nullptr);
    for (const std::string& w : warnings) {
        warn_once(host_path + w, host_path + ": " + w);
    }
    if (!loaded) {
        warn_once(host_path, host_path + ": " + loaded.error() + "; the character is drawn from its disc model");
        return nullptr;
    }
    auto skin = std::make_unique<sh::assets::SkinModel>(std::move(loaded.value()));
    std::fprintf(stderr, "[MODEL] %s: %zu vertices, %zu groups, %zu materials, %zu textures, %d bones\n", host,
                 skin->vertices.size(), skin->groups.size(), skin->materials.size(), skin->textures.size(),
                 skin->bone_count);
    return skin;
}

void prepare(Model& m) {
    const auto& verts = m.skin->vertices;
    m.normal.resize(verts.size());
    m.dominant.resize(verts.size());
    for (std::size_t v = 0; v < verts.size(); ++v) {
        // The file's normals point out of the body; the game's light takes them pointing in.
        std::int32_t q[3];
        for (int k = 0; k < 3; ++k) {
            q[k] = static_cast<std::int32_t>(std::lroundf(-verts[v].normal[static_cast<std::size_t>(k)] * 4096.0f));
            q[k] = q[k] < -32767 ? -32767 : (q[k] > 32767 ? 32767 : q[k]);
        }
        m.normal[v] = {static_cast<std::int16_t>(q[0]), static_cast<std::int16_t>(q[1]),
                       static_cast<std::int16_t>(q[2]), 0};
        m.dominant[v] = static_cast<std::uint8_t>(verts[v].dominant);
    }
    std::vector<std::string> keys(verts.size());
    std::vector<std::array<float, 3>> normals(verts.size());
    for (std::size_t v = 0; v < verts.size(); ++v) {
        const sh::assets::SkinVertex& sv = verts[v];
        std::string& k = keys[v];
        for (int q = 0; q < sv.count; ++q) {
            const sh::assets::SkinInfluence& in = sv.influence[static_cast<std::size_t>(q)];
            k.append(reinterpret_cast<const char*>(&in.bone), sizeof in.bone);
            k.append(reinterpret_cast<const char*>(&in.weight), sizeof in.weight);
            k.append(reinterpret_cast<const char*>(in.local.data()), sizeof in.local);
        }
        normals[v] = sv.normal;
    }
    m.weld = sh::scene::pn_weld(keys, normals);
    m.triangles.clear();
    for (const sh::assets::SkinGroup& src : m.skin->groups)
        m.triangles.insert(m.triangles.end(), src.indices.begin(), src.indices.end());
    std::vector<int> slot(verts.size());
    m.groups.clear();
    for (const sh::assets::SkinGroup& src : m.skin->groups) {
        Group g;
        g.material = src.material;
        std::fill(slot.begin(), slot.end(), -1);
        for (std::uint32_t index : src.indices) {
            if (slot[index] < 0) {
                slot[index] = static_cast<int>(g.verts.size());
                g.verts.push_back(static_cast<int>(index));
            }
            g.tris.push_back(static_cast<std::uint32_t>(slot[index]));
        }
        m.groups.push_back(std::move(g));
    }
    m.parts_of = nullptr;
    m.has_solid_material = false;
    for (const sh::assets::SkinMaterial& mat : m.skin->materials) {
        m.has_solid_material = m.has_solid_material || !mat.semi_transparent;
    }
}

void match_parts(Model& m, const SkeletonModel& sm) {
    if (m.parts_of == sm.model) {
        return;
    }
    m.parts_of = sm.model;
    for (std::size_t g = 0; g < m.groups.size(); ++g) {
        const std::string& node = m.skin->groups[g].node;
        // The importer names the part by index (node extras part_index); a file without it is matched by name.
        const int indexed = m.skin->groups[g].part;
        m.groups[g].part = indexed < sm.part_count && indexed < kMaxParts ? indexed : -1;
        for (int p = 0; indexed < 0 && p < sm.part_count && p < kMaxParts; ++p) {
            char name[9];
            model_part_name(sm.model, p, name);
            if (node == name) {
                m.groups[g].part = p;
                break;
            }
        }
    }
}

Model* find_model(const std::string& key) {
    auto& list = models();
    // `mods reload` made the scene forget its files: the characters' files are read again too.
    static std::uint32_t generation = sh::scene::asset_generation();
    if (generation != sh::scene::asset_generation()) {
        generation = sh::scene::asset_generation();
        instance().model = nullptr;
        list.clear();
    }
    for (auto& m : list) {
        if (m->key == key) {
            return m->skin ? m.get() : nullptr;
        }
    }
    if (static_cast<int>(list.size()) >= kMaxModels) {
        return nullptr;
    }
    list.push_back(std::make_unique<Model>());
    Model& m = *list.back();
    m.key = key;
    std::string host;
    m.skin = load(key, host, m.from_mod);
    if (!m.skin) {
        return nullptr;
    }
    if (m.skin->bone_count > kMaxBones) {
        std::fprintf(stderr, "warning: [MODEL] %s: more than %d bones or out of memory; drawn from the disc model\n",
                     host.c_str(), kMaxBones);
        m.skin.reset();
        return nullptr;
    }
    prepare(m);
    PoseScratch& s = scratch();
    if (m.skin->vertices.size() > s.ok.size()) {
        s.xyz.resize(m.skin->vertices.size() * 3);
        s.ok.resize(m.skin->vertices.size());
        s.rgb.resize(m.skin->vertices.size() * 3);
    }
    return &m;
}

// The view transform of a bone: the frame's camera applied to the bone's world transform. The camera is the
// unrounded twin every draw path shares (the map, the world objects, the effects) whenever it stands for the view
// system's camera, so a character keeps its place against them at any distance and under any turn; the world
// transform is the smooth_models twin when it agrees with the integers, else the integers (`frame` is null for a
// bone that has no twin). Without either the integer view matrix is the transform.
void store_bone(Instance& in, int b, void* frame, const Transform& world, const Transform& view) {
    sh::assets::BoneTransform& o = in.bones[static_cast<std::size_t>(b)];
    in.have[static_cast<std::size_t>(b)] = 1;
    static const std::array<double, 3> kNoShift{0.0, 0.0, 0.0};
    const std::array<double, 3>& shift = in.shifted ? in.shift[static_cast<std::size_t>(b)] : kNoShift;

    double wr[9], wt[3];
    bool exact_world = frame != nullptr && Pc_Smooth_Active() && Pc_Smooth_WorldOf(frame, wr, wt) != 0;
    // Anything further off than the rounding means the twin misread this model's hierarchy.
    for (int r = 0; r < 3 && exact_world; ++r) {
        for (int c = 0; c < 3; ++c) {
            if (std::fabs(wr[r * 3 + c] - world.m[r][c]) > 48.0) exact_world = false;
        }
        if (std::fabs(wt[r] - world.t[r]) > 24.0) exact_world = false;
    }

    const sh::camera::WorldScreen ws = sh::camera::view_system().world_screen();
    const sh::camera::Matrix& wv = ws.world_to_view;
    sh::math::Transform key;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) key.rot.m[r][c] = ws.world_screen.m[r][c];
    }
    key.trans = {ws.world_screen.t[0], ws.world_screen.t[1], ws.world_screen.t[2]};
    const sh::math::capture::CameraTwin* twin = sh::math::capture::camera_of(key.rot, key.trans);

    if (twin == nullptr && !exact_world) {
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                o.m[static_cast<std::size_t>(r * 3 + c)] = view.m[r][c] / 4096.0f;
            }
            o.t[static_cast<std::size_t>(r)] = static_cast<float>(
                view.t[r] + (wv.m[r][0] * shift[0] + wv.m[r][1] * shift[1] + wv.m[r][2] * shift[2]) / 4096.0);
        }
        return;
    }
    if (!exact_world) {
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) wr[r * 3 + c] = world.m[r][c];
            wt[r] = world.t[r];
        }
    }
    double cr[9], d[3];
    for (int k = 0; k < 9; ++k) cr[k] = twin != nullptr ? twin->rot[k] * 4096.0 : wv.m[k / 3][k % 3];
    for (int r = 0; r < 3; ++r) {
        d[r] = wt[r] + shift[static_cast<std::size_t>(r)] - (twin != nullptr ? twin->eye[r] : ws.eye.t[r]);
    }
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            const double q = (cr[r * 3] * wr[c] + cr[r * 3 + 1] * wr[3 + c] + cr[r * 3 + 2] * wr[6 + c]) / 4096.0;
            o.m[static_cast<std::size_t>(r * 3 + c)] = static_cast<float>(q / 4096.0);
        }
        const double t = (cr[r * 3] * d[0] + cr[r * 3 + 1] * d[1] + cr[r * 3 + 2] * d[2]) / 4096.0 + wv.t[r];
        o.t[static_cast<std::size_t>(r)] = static_cast<float>(t);
    }
}

void light_bone(Instance& in, int b, const Transform* world) {
    sh::world::ModelFrame frame;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            frame.m[r][c] = world->m[r][c];
        }
        frame.t[r] = world->t[r];
        if (in.shifted) {
            frame.t[r] += static_cast<std::int32_t>(std::lround(in.shift[static_cast<std::size_t>(b)][static_cast<std::size_t>(r)]));
        }
    }
    const BoneLight l = sh::world::bone_light(sh::world::atmosphere(), &frame);
    in.light[static_cast<std::size_t>(b)] = l;
    if (sh::world::atmosphere().state().light_mode != 0) {
        std::memcpy(in.color.data(), l.color, sizeof(l.color));
    }
    in.lit[static_cast<std::size_t>(b)] = 1;
}

// The coprocessor's single-normal colour operation (sf = 1, lm = 1) on the light the bone's prelude left.
void shade(const Instance& in, const BoneLight& l, const ShortVec4& n, std::uint8_t* rgb) {
    std::int32_t ir[3];
    for (int k = 0; k < 3; ++k) {
        const std::int32_t mac = static_cast<std::int32_t>(
            (static_cast<std::int64_t>(l.light[k * 3]) * n.x + static_cast<std::int64_t>(l.light[k * 3 + 1]) * n.y +
             static_cast<std::int64_t>(l.light[k * 3 + 2]) * n.z) >> 12);
        ir[k] = mac < 0 ? 0 : (mac > 0x7FFF ? 0x7FFF : mac);
    }
    for (int k = 0; k < 3; ++k) {
        std::int32_t mac = static_cast<std::int32_t>(
            ((static_cast<std::int64_t>(l.back[k]) << 12) + static_cast<std::int64_t>(in.color[k * 3]) * ir[0] +
             static_cast<std::int64_t>(in.color[k * 3 + 1]) * ir[1] +
             static_cast<std::int64_t>(in.color[k * 3 + 2]) * ir[2]) >> 12);
        mac >>= 4;
        rgb[k] = static_cast<std::uint8_t>(mac < 0 ? 0 : (mac > 255 ? 255 : mac));
    }
}

// Model upscaling: every vertex's patch normal in view space, from the file's normal turned by its dominant bone.
void patch_normals(const Instance& in, const Model& m, std::vector<std::array<float, 4>>& out) {
    const auto& verts = m.skin->vertices;
    std::vector<std::array<float, 3>> view(verts.size());
    for (std::size_t v = 0; v < verts.size(); ++v) {
        const auto b = static_cast<std::size_t>(m.dominant[v]);
        const std::array<float, 9>& r = in.bones[b < in.bones.size() ? b : 0].m;
        const std::array<float, 3>& n = verts[v].normal;
        for (std::size_t k = 0; k < 3; ++k) view[v][k] = r[k * 3] * n[0] + r[k * 3 + 1] * n[1] + r[k * 3 + 2] * n[2];
    }
    sh::scene::pn_normals(m.weld, view, std::span<const float>(scratch().xyz.data(), verts.size() * 3), m.triangles,
                          out);
}

// One view-space mesh per material of the posed character, placed at `key`.
void submit(const Instance& in, const Model& m, bool hide_head, sh::frame::OrderKey key) {
    const PoseScratch& s = scratch();
    const int material_count = static_cast<int>(m.skin->materials.size());
    // The meshes of one character share one upscaling level (PrelitMesh::upscale_model).
    static std::uint32_t upscale_model = 0;
    static std::vector<std::array<float, 4>> pn;
    const bool upscale = sh::scene::upscale_active() != sh::scene::UpscaleMode::Off && !m.from_mod;
    if (upscale) {
        upscale_model = upscale_model == 0xFFFFFFFFu ? 1u : upscale_model + 1u;
        patch_normals(in, m, pn);
    }
    for (int mi = -1; mi < material_count; ++mi) {
        sh::scene::PrelitMesh mesh;
        if (upscale) mesh.upscale_model = upscale_model;
        for (const Group& g : m.groups) {
            const int gv = static_cast<int>(g.verts.size());
            const int gi = static_cast<int>(g.tris.size());
            if (g.material != mi || (g.part >= 0 && !in.part_visible[static_cast<std::size_t>(g.part)]) || gv == 0 ||
                gi < 3) {
                continue;
            }
            const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
            for (int t = 0; t + 2 < gi; t += 3) {
                const int a = g.verts[g.tris[static_cast<std::size_t>(t)]];
                const int b = g.verts[g.tris[static_cast<std::size_t>(t + 1)]];
                const int c = g.verts[g.tris[static_cast<std::size_t>(t + 2)]];
                if (!s.ok[static_cast<std::size_t>(a)] || !s.ok[static_cast<std::size_t>(b)] ||
                    !s.ok[static_cast<std::size_t>(c)]) {
                    continue;
                }
                if (hide_head && m.dominant[static_cast<std::size_t>(a)] == 2 &&
                    m.dominant[static_cast<std::size_t>(b)] == 2 && m.dominant[static_cast<std::size_t>(c)] == 2) {
                    continue;
                }
                for (int k = 0; k < 3; ++k) {
                    mesh.indices.push_back(base + g.tris[static_cast<std::size_t>(t + k)]);
                }
            }
            for (int vi : g.verts) {
                const std::size_t v = static_cast<std::size_t>(vi);
                const auto& uv = m.skin->vertices[v].uv;
                sh::scene::PrelitVertex o;
                o.x = s.xyz[v * 3];
                o.y = s.xyz[v * 3 + 1];
                o.z = s.xyz[v * 3 + 2];
                o.u = uv[0];
                o.v = uv[1];
                o.r = s.rgb[v * 3];
                o.g = s.rgb[v * 3 + 1];
                o.b = s.rgb[v * 3 + 2];
                o.a = 255;
                mesh.vertices.push_back(o);
                if (upscale) mesh.patch_normals.push_back(pn[v]);
            }
        }
        if (mesh.indices.size() < 3) {
            continue;
        }
        if (mi >= 0) {
            const sh::assets::SkinMaterial& mat = m.skin->materials[static_cast<std::size_t>(mi)];
            if (mat.texture >= 0 && mat.texture < static_cast<int>(m.skin->textures.size())) {
                const sh::assets::SkinTexture& tex = m.skin->textures[static_cast<std::size_t>(mat.texture)];
                if (!tex.rgba.empty()) {
                    mesh.texture_key = texture_key(m, mat.texture);
                    sh::scene::add_prelit_texture(mesh.texture_key, tex.width, tex.height, tex.rgba.data());
                }
            }
            // PS1 semi-transparency: the file's semi-transparent materials blend their STP texels with the mode
            // the game bound the character's texture with, unless the game switched the whole model's translucency.
            bool semi = mat.semi_transparent;
            bool all_stp = false;
            if (in.blend.translucent == 0) {
                semi = false;
            } else if (in.blend.translucent == 1 && m.has_solid_material) {
                semi = true;
                all_stp = true;
            }
            if (semi) {
                mesh.blend = sh::scene::blend_of_abr(mat.abr >= 0 ? mat.abr : in.blend.abr);
                mesh.split_stp = true;
                mesh.all_stp = all_stp;
            } else if (!mat.semi_transparent && mat.alpha == sh::assets::SkinAlphaMode::Blend) {
                mesh.blend = sh::scene::Blend::Average;
            }
        }
        // The fade of the disc parts this mesh stands in for: the beam lights the character like the model it
        // replaces.
        mesh.fade = sh::frame::state().chara_fade;
        mesh.casts_shadow = !sh::frame::state().no_shadow_cast;
        auto& prelit = sh::scene::current_frame().prelit;
        prelit.push_back(std::move(mesh));
        sh::frame::submit_keyed(sh::frame::Kind::Scene, static_cast<std::uint32_t>(prelit.size()), key);
    }
}

// Where the joints of a model with its own proportions are, against the game's skeleton (SkinModel::joint_shifts),
// with the bones turned as the game has them now: `out[b]` is the world-space difference for the first `count` bones.
void bone_shifts(const sh::assets::SkinModel& skin, void* frames, int count,
                 std::array<std::array<double, 3>, kMaxBones>& out) {
    std::array<BoneRest, kMaxBones> rest;
    out.fill({0.0, 0.0, 0.0});
    count = count < kMaxBones ? count : kMaxBones;
    frames_rest(frames, rest.data(), count);
    skin.joint_shifts(rest.data(), count, 4096.0, [frames](int bone, double rot[9]) {
        void* frame = game_frame_at(frames, bone);
        Transform world, view;
        game_frame_world_and_view(frame, world, view);
        double at[3];
        bool exact = Pc_Smooth_Active() && Pc_Smooth_WorldOf(frame, rot, at) != 0;
        for (int i = 0; i < 9 && exact; ++i) {
            if (std::fabs(rot[i] - world.m[i / 3][i % 3]) > 48.0) exact = false;
        }
        for (int i = 0; i < 9 && !exact; ++i) {
            rot[i] = world.m[i / 3][i % 3];
        }
    }, out.data());
}

bool begin(Instance& in, const void* skeleton, void* frames) {
    SkeletonModel sm;
    if (!skeleton_model(skeleton, sm)) {
        return false;
    }
    Model* m = find_model(sm.glb);
    if (m == nullptr) {
        return false;
    }
    match_parts(*m, sm);

    in.have.fill(0);
    in.lit.fill(0);
    in.part_visible.fill(0);
    in.model = m;
    in.parts_model = sm.model;
    in.frames = frames;
    in.bone_limit = sm.bone_count < kMaxBones ? sm.bone_count : kMaxBones;
    in.light_mode = sh::world::atmosphere().state().light_mode;
    in.blend = model_blend_state(sm.model);
    in.shifted = m->skin->own_proportions;
    if (in.shifted) {
        bone_shifts(*m->skin, frames, in.bone_limit, in.shift);
    }
    return true;
}

// One part the skeleton shows: its bone's matrices and light, unless a part before it brought them.
void part(Instance& in, const SkeletonPartRef& ref) {
    void* frame = game_frame_at(in.frames, ref.bone);
    Transform world, view;
    game_frame_world_and_view(frame, world, view);
    const PartState ps = skeleton_part(in.parts_model, ref.part);
    // A collapsed part draws nothing; its bone, when a vertex needs it, comes from the frames at the end.
    if (ps.collapsed) {
        return;
    }
    Pc_Smooth_CoordView(frame, &view);
    if (ps.index >= 0 && ps.index < kMaxParts) {
        in.part_visible[static_cast<std::size_t>(ps.index)] = 1;
    }
    if (ref.bone < 0 || ref.bone >= kMaxBones || in.have[static_cast<std::size_t>(ref.bone)]) {
        return;
    }
    void* twin = ref.bone < in.bone_limit ? frame : nullptr;
    store_bone(in, ref.bone, twin, world, view);
    light_bone(in, ref.bone, &world);
}

void end(Instance& in, void* /*ot: the world layer*/, int shift, bool hide_head) {
    Model& m = *in.model;
    in.model = nullptr;

    // Bones the file uses that no visible part brought (seam vertices owned by a hidden part, or joints an HD model
    // skins to).
    for (int b = 0; b < m.skin->bone_count && b < in.bone_limit; ++b) {
        if (in.have[static_cast<std::size_t>(b)]) {
            continue;
        }
        Transform world, view;
        void* frame = game_frame_at(in.frames, b);
        game_frame_world_and_view(frame, world, view);
        store_bone(in, b, frame, world, view);
        light_bone(in, b, &world);
    }

    PoseScratch& s = scratch();
    m.skin->pose(in.bones.data(), in.bones.size(), in.have.data(), s.xyz.data(), s.ok.data());
    float min_z = 1e30f, max_z = 0.0f;
    const std::size_t count = m.skin->vertices.size();
    for (std::size_t v = 0; v < count; ++v) {
        const float z = s.xyz[v * 3 + 2];
        const std::size_t db = m.dominant[v];
        std::uint8_t* rgb = &s.rgb[v * 3];
        if (!s.ok[v]) {
            continue;
        }
        if (z < 16.0f || z > 65535.0f) {
            s.ok[v] = 0;
            continue;
        }
        min_z = z < min_z ? z : min_z;
        max_z = z > max_z ? z : max_z;
        if (!in.lit[db]) {
            rgb[0] = rgb[1] = rgb[2] = 128;
        } else if (in.light_mode == 0) {
            std::memcpy(rgb, in.light[db].ambient, 3);
        } else {
            shade(in, in.light[db], m.normal[v], rgb);
        }
    }
    if (max_z <= 0.0f) {
        return;
    }
    sh::frame::state().note_item_depth(static_cast<unsigned>(max_z));
    const int bucket = kOtBase + ((static_cast<int>(min_z) >> shift) >> 2);
    submit(in, m, hide_head, world_order_key(bucket));
}

}  // namespace
}  // namespace sh::render

using namespace sh::render;

extern "C" int Pc_GltfChara_BoneShift(const void* skeleton, void* boneFrames, int bone, int out[3]) {
    out[0] = out[1] = out[2] = 0;
    SkeletonModel sm;
    if (skeleton == nullptr || boneFrames == nullptr || bone < 0 || !skeleton_model(skeleton, sm)) {
        return 0;
    }
    const Model* m = find_model(sm.glb);
    if (m == nullptr || !m->skin->own_proportions || bone >= sm.bone_count || bone >= kMaxBones) {
        return 0;
    }
    static std::array<std::array<double, 3>, kMaxBones> shift;
    bone_shifts(*m->skin, boneFrames, sm.bone_count, shift);
    for (int c = 0; c < 3; ++c) {
        out[c] = static_cast<int>(std::lround(shift[static_cast<std::size_t>(bone)][static_cast<std::size_t>(c)]));
    }
    return 1;
}

extern "C" void Pc_GltfChara_Draw(const void* skeleton, void* boneFrames, void* ot, int shift, int hideHead) {
    if (skeleton == nullptr || boneFrames == nullptr) {
        return;
    }
    std::array<SkeletonPartRef, 128> parts;
    const int count = skeleton_parts(skeleton, parts.data(), static_cast<int>(parts.size()));
    if (count <= 0) {
        return;
    }
    Instance& in = instance();
    if (!begin(in, skeleton, boneFrames)) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        part(in, parts[static_cast<std::size_t>(i)]);
    }
    end(in, ot, shift, hideHead != 0);
}

namespace sh::gltf_chara {

void refresh_textures(const std::function<bool(const std::string& path)>& match) {
    for (const std::unique_ptr<Model>& m : models()) {
        if (!m || !m->skin) continue;
        for (std::size_t i = 0; i < m->skin->textures.size(); ++i) {
            sh::assets::SkinTexture& tex = m->skin->textures[i];
            if (tex.file.empty() || !match(tex.file.generic_string()) || !sh::assets::reload_skin_texture(tex))
                continue;
            sh::scene::refresh_prelit_texture(texture_key(*m, static_cast<int>(i)));
        }
    }
}

}  // namespace sh::gltf_chara
