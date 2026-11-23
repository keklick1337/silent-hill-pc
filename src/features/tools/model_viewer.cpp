// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The editor's model viewer: a character's skinned glb or a library model (world
// object, item, held weapon) in a viewport of its own. The picture is drawn by the engine's scene renderer off
// screen (render/gpu/offscreen_scene.h) from a scene frame built here: an orbit camera, flat or lit, wireframe, and
// for a character its animation file's clips on a timeline (the ANM keyframes posed with the file's own arithmetic,
// formats/convert/anim) with the skeleton drawn over the picture. Read only: nothing of the game is posed or moved.

#include "features/tools/asset_views.h"
#include "features/tools/tools.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <map>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "engine/assets/glb_anims.h"
#include "engine/assets/skin_model.h"
#include "engine/chara/chara_library.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/console/instance.h"
#include "engine/console/registry.h"
#include "engine/vfs/vfs_c.h"
#include "formats/common/json.h"
#include "formats/convert/anim/anim_clips.h"
#include "formats/convert/anim/anim_json.h"
#include "formats/convert/anim/anm_file.h"
#include "render/gpu/offscreen_scene.h"
#include "render/scene/area_cells.h"
#include "render/scene/scene.h"

namespace sh::tools::assets {

namespace {

namespace fs = std::filesystem;
namespace anim = sh::convert::anim;

constexpr float kFovDeg = 40.0f;

// --- the characters' models, shared with the world inspector ---

struct CharaEntry {
    bool pending = false;
    bool done = false;
    std::string error;
    std::string host;  // the glb read
    std::unique_ptr<sh::assets::SkinModel> model;
    std::vector<std::unique_ptr<Picture>> pictures;  // by SkinModel::textures
    std::vector<std::uint64_t> keys;                 // posed-mesh texture keys, by SkinModel::textures
    std::optional<anim::AnmFile> anm;
    std::string anm_path;   // game path ("ANIM/HB_BASE.ANM"): the clip table's key
    std::string error_anim; // why there is no animation file
    std::vector<anim::Clip> clips;
    // The camera's frame: the whole file's first keyframe, so the camera stays put while a clip walks or turns.
    bool framed = false;
    std::array<double, 3> centre{0, 0, 0};
    double radius = 1;
};

std::map<std::string, CharaEntry>& charas() {
    static auto* s = new std::map<std::string, CharaEntry>;  // pictures live in the overlay; kept to the exit
    static std::uint32_t s_epoch = asset_epoch();
    if (s_epoch != asset_epoch()) {
        s->clear();
        s_epoch = asset_epoch();
    }
    return *s;
}

std::uint64_t key_of(const std::string& text) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : text) h = (h ^ c) * 1099511628211ull;
    return h | 1ull;  // 0 means untextured
}

fs::path resolve_converted(const fs::path& resolved) {
    const fs::path rel = sh::assets::below_convert(resolved);
    if (rel.empty()) return resolved;
    char host[1024];
    return ShVfs_FindConverted(rel.generic_string().c_str(), host, sizeof host) ? fs::path(host) : resolved;
}

const sh::chara::CharacterDef* chara_def(const std::string& folder) {
    const sh::chara::CharaLibrary* lib = Pc_CharaData_Library();
    return lib != nullptr ? lib->find_by_id(folder) : nullptr;
}

void load_chara(const std::string& folder) {
    CharaEntry& e = charas()[folder];
    e.done = true;
    e.pending = false;
    const sh::chara::CharacterDef* def = chara_def(folder);
    if (def == nullptr || def->model.empty()) {
        e.error = "no model in its character.json";
        return;
    }
    char host[1024];
    if (!ShVfs_FindConverted(def->model.c_str(), host, sizeof host)) {
        e.error = def->model + " is not in gamedata/convert or a mod";
        return;
    }
    e.host = host;
    std::vector<std::string> warnings;
    auto loaded = sh::assets::load_skin_model(host, resolve_converted, warnings);
    if (!loaded) {
        e.error = loaded.error();
        return;
    }
    e.model = std::make_unique<sh::assets::SkinModel>(std::move(loaded.value()));
    for (std::size_t i = 0; i < e.model->textures.size(); ++i) {
        const sh::assets::SkinTexture& t = e.model->textures[i];
        auto pic = std::make_unique<Picture>();
        pic->set(t.width, t.height, t.rgba.data());
        e.pictures.push_back(std::move(pic));
        const std::uint64_t key = key_of("tools.model_viewer:" + e.host + "#" + std::to_string(i) + "@" +
                                         std::to_string(asset_epoch()));
        scene::add_prelit_texture(key, t.width, t.height, t.rgba.data());
        e.keys.push_back(key);
    }
    // The animation file: the converted JSON the character names, else the disc file packed into its glb.
    if (!def->animations.empty() && ShVfs_FindConverted(def->animations.c_str(), host, sizeof host)) {
        std::ifstream in(host, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (auto file = anim::anm_from_json(text); file && !file.value().extension()) {
            e.anm = std::move(file.value());
            if (auto doc = sh::json::parse(text); doc && doc.value().is_object()) {
                const sh::json::Value* src = doc.value().find("source");
                const sh::json::Value* path = src != nullptr ? src->find("path") : nullptr;
                if (path != nullptr && path->is_string()) e.anm_path = path->string();
            }
        } else {
            e.error_anim = def->animations + (file ? std::string(": keyframes only") : ": " + file.error());
        }
    } else if (!def->disc_anim.empty()) {
        if (auto packed = sh::assets::packed_anm(e.host, def->disc_anim); packed && packed.value().base.empty()) {
            e.anm = std::move(packed.value().file);
            e.anm_path = def->disc_anim;
        }
    }
    if (e.anm && !e.anm_path.empty()) e.clips = anim::clips_for(anim::builtin_clips(), e.anm_path);
}

CharaEntry* chara_entry(const std::string& folder) {
    if (folder.empty()) return nullptr;
    CharaEntry& e = charas()[folder];
    if (!e.done && !e.pending) {
        e.pending = true;
        defer([folder] { load_chara(folder); });
    }
    return e.done ? &e : nullptr;
}

// --- the viewer ---

enum class Source { None, Character, Library };

struct Viewer {
    Source source = Source::None;
    std::string folder;              // Character
    std::string file, name;          // Library
    std::shared_ptr<const scene::LibraryModel> library;
    bool library_pending = false;
    std::uint32_t library_epoch = 0;
    float yaw = 210.0f, pitch = -12.0f, zoom = 1.0f;
    bool lit = true, wireframe = false, skeleton = false;
    int clip = -1;          // index into the character's clips, -1: the whole file
    float keyframe = 0.0f;  // within the clip
    bool playing = true;
    float speed = 1.0f;
    std::vector<std::uint8_t> parts_off;  // per SkinGroup
    Picture picture;
    int picture_w = 0, picture_h = 0;
    char filter[64] = "";
    std::string group = "all";  // "all", "chara", "model" or a model file's folder
    float below_list = 0.0f;    // last frame's height of everything under the list
    // The last frame's skeleton in picture pixels (parent, child), for the overlay of the picture.
    std::vector<std::array<float, 4>> bones;
};

Viewer& viewer_state() {
    static auto* v = new Viewer;
    return *v;
}

// Model-to-view rotation of the orbit (row-major): pitch about x after yaw about y.
std::array<double, 9> orbit(double yaw_deg, double pitch_deg) {
    const double y = yaw_deg * std::numbers::pi / 180.0, p = pitch_deg * std::numbers::pi / 180.0;
    const double cy = std::cos(y), sy = std::sin(y), cp = std::cos(p), sp = std::sin(p);
    // Rx(p) * Ry(y)
    return {cy, 0, sy, sp * sy, cp, -sp * cy, -cp * sy, sp, cp * cy};
}

std::array<double, 3> mul(const std::array<double, 9>& m, const std::array<double, 3>& v) {
    return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
            m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}

// Toward the light, view axes (y down, z away): up, left and in front of the model.
constexpr std::array<double, 3> kToLight{-0.42, -0.62, -0.66};

struct Camera {
    std::array<double, 9> rot;
    std::array<double, 3> centre;
    double distance = 1;
    double focal = 1;  // pixels
};

Camera camera_for(const Viewer& v, const std::array<double, 3>& centre, double radius, int h) {
    Camera c;
    c.rot = orbit(v.yaw, v.pitch);
    c.centre = centre;
    const double half = std::tan(kFovDeg * 0.5 * std::numbers::pi / 180.0);
    c.distance = std::max(radius, 16.0) / half * 1.15 / std::max(v.zoom, 0.05f);
    c.focal = h * 0.5 / half;
    return c;
}

std::array<double, 3> to_view(const Camera& c, const std::array<double, 3>& p) {
    const std::array<double, 3> r = mul(c.rot, {p[0] - c.centre[0], p[1] - c.centre[1], p[2] - c.centre[2]});
    return {r[0], r[1], r[2] + c.distance};
}

const std::vector<anim::Clip>* clips_of(const CharaEntry& e) { return e.anm ? &e.clips : nullptr; }

// The clip's keyframe range: the whole file without a clip.
void clip_range(const CharaEntry& e, int clip, int* start, int* end) {
    *start = 0;
    *end = e.anm ? std::max(0, static_cast<int>(e.anm->keyframes().size()) - 1) : 0;
    if (clip >= 0 && static_cast<std::size_t>(clip) < e.clips.size()) {
        *start = e.clips[static_cast<std::size_t>(clip)].start;
        *end = e.clips[static_cast<std::size_t>(clip)].end;
    }
}

// The character posed at the viewer's keyframe into posed meshes of `frame`; false when it cannot be posed.
bool build_character(Viewer& v, CharaEntry& e, int w, int h, scene::Frame& frame, std::string* why) {
    const sh::assets::SkinModel& model = *e.model;
    if (!e.anm) {
        *why = "no animation file" + (e.error_anim.empty() ? std::string(" named") : " (" + e.error_anim + ")") +
               ": the model cannot be posed";
        return false;
    }
    int start = 0, end = 0;
    clip_range(e, v.clip, &start, &end);
    const int span = std::max(0, end - start);
    v.keyframe = std::clamp(v.keyframe, 0.0f, static_cast<float>(span));
    const int k0 = start + static_cast<int>(std::floor(v.keyframe));
    const int k1 = std::min(k0 + 1, end);
    const auto alpha = static_cast<std::int32_t>((v.keyframe - std::floor(v.keyframe)) * 4096.0f);
    std::vector<anim::Pose> world = e.anm->world_poses(e.anm->local_poses(static_cast<std::size_t>(k0),
                                                                           static_cast<std::size_t>(k1), alpha));
    if (model.own_proportions) {
        // The model's own bone lengths under the clip's rotations, as the game draws it (gltf_chara.h).
        const std::vector<anim::AnmBone>& ab = e.anm->bones();
        const std::size_t n = std::min(world.size(), ab.size());
        std::vector<sh::assets::BoneRest> rest(n);
        for (std::size_t i = 0; i < n; ++i) {
            const int parent = ab[i].parent;
            rest[i].parent = parent >= 0 && static_cast<std::size_t>(parent) < n && static_cast<std::size_t>(parent) != i ? parent : -1;
            rest[i].animated = i == 0 || rest[i].parent < 0 || ab[i].translation >= 0;
            for (std::size_t c = 0; c < 3; ++c) {
                rest[i].offset[c] = static_cast<float>(ab[i].bind[c] * (1 << e.anm->header().scale_log2));
            }
        }
        std::vector<std::array<double, 3>> shift(n);
        model.joint_shifts(rest.data(), static_cast<int>(n), 4096.0, [&world](int bone, double rot[9]) {
            for (std::size_t k = 0; k < 9; ++k) rot[k] = world[static_cast<std::size_t>(bone)].m[k];
        }, shift.data());
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t c = 0; c < 3; ++c) world[i].t[c] += static_cast<std::int32_t>(std::lround(shift[i][c]));
        }
    }
    std::vector<sh::assets::BoneTransform> bones(std::max<std::size_t>(world.size(), static_cast<std::size_t>(model.bone_count)));
    std::vector<std::uint8_t> have(bones.size(), 0);
    for (std::size_t i = 0; i < world.size(); ++i) {
        for (int k = 0; k < 9; ++k) bones[i].m[static_cast<std::size_t>(k)] = static_cast<float>(world[i].m[static_cast<std::size_t>(k)]) / 4096.0f;
        for (int k = 0; k < 3; ++k) bones[i].t[static_cast<std::size_t>(k)] = static_cast<float>(world[i].t[static_cast<std::size_t>(k)]);
        have[i] = 1;
    }
    std::vector<float> xyz;
    std::vector<std::uint8_t> ok;
    model.pose(bones, have, xyz, ok);

    if (!e.framed) {
        std::array<double, 3> lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30};
        for (std::size_t i = 0; i < ok.size(); ++i) {
            if (!ok[i]) continue;
            for (std::size_t k = 0; k < 3; ++k) {
                lo[k] = std::min(lo[k], static_cast<double>(xyz[i * 3 + k]));
                hi[k] = std::max(hi[k], static_cast<double>(xyz[i * 3 + k]));
            }
        }
        e.centre = {(lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5, (lo[2] + hi[2]) * 0.5};
        e.radius = 0.5 * std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) +
                                   (hi[2] - lo[2]) * (hi[2] - lo[2]));
        e.framed = true;
    }
    const Camera cam = camera_for(v, e.centre, e.radius, h);
    frame.h = static_cast<float>(cam.focal);
    frame.geom_ofs_x = w * 0.5f;
    frame.geom_ofs_y = h * 0.5f;

    if (v.parts_off.size() != model.groups.size()) v.parts_off.assign(model.groups.size(), 0);
    std::vector<std::int32_t> remap(model.vertices.size(), -1);
    for (std::size_t gi = 0; gi < model.groups.size(); ++gi) {
        const sh::assets::SkinGroup& g = model.groups[gi];
        if (v.parts_off[gi]) continue;
        const sh::assets::SkinMaterial* mat =
            g.material >= 0 && static_cast<std::size_t>(g.material) < model.materials.size()
                ? &model.materials[static_cast<std::size_t>(g.material)] : nullptr;
        scene::PrelitMesh mesh;
        if (mat && mat->texture >= 0 && static_cast<std::size_t>(mat->texture) < e.keys.size())
            mesh.texture_key = e.keys[static_cast<std::size_t>(mat->texture)];
        mesh.blend = mat && mat->alpha == sh::assets::SkinAlphaMode::Blend ? scene::Blend::Average : scene::Blend::Opaque;
        mesh.casts_shadow = false;
        std::fill(remap.begin(), remap.end(), -1);
        for (std::size_t t = 0; t + 2 < g.indices.size(); t += 3) {
            const std::uint32_t tri[3] = {g.indices[t], g.indices[t + 1], g.indices[t + 2]};
            bool good = true;
            for (std::uint32_t i : tri) good = good && i < ok.size() && ok[i];
            if (!good) continue;
            for (std::uint32_t i : tri) {
                if (remap[i] < 0) {
                    const sh::assets::SkinVertex& sv = model.vertices[i];
                    const std::array<double, 3> p = to_view(cam, {xyz[i * 3], xyz[i * 3 + 1], xyz[i * 3 + 2]});
                    scene::PrelitVertex pv;
                    pv.x = static_cast<float>(p[0]);
                    pv.y = static_cast<float>(p[1]);
                    pv.z = static_cast<float>(p[2]);
                    pv.u = sv.uv[0];
                    pv.v = sv.uv[1];
                    double level = 1.0;
                    if (v.lit) {
                        const sh::assets::BoneTransform& b =
                            bones[static_cast<std::size_t>(std::clamp(sv.dominant, 0, static_cast<int>(bones.size()) - 1))];
                        const std::array<double, 3> nb{b.m[0] * sv.normal[0] + b.m[1] * sv.normal[1] + b.m[2] * sv.normal[2],
                                                       b.m[3] * sv.normal[0] + b.m[4] * sv.normal[1] + b.m[5] * sv.normal[2],
                                                       b.m[6] * sv.normal[0] + b.m[7] * sv.normal[1] + b.m[8] * sv.normal[2]};
                        const std::array<double, 3> n = mul(cam.rot, nb);
                        const double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                        const double d = len > 0 ? (n[0] * kToLight[0] + n[1] * kToLight[1] + n[2] * kToLight[2]) / len : 0;
                        level = 0.42 + 0.7 * std::max(0.0, d);
                    }
                    const std::array<float, 4> base = mat ? mat->base_color : std::array<float, 4>{1, 1, 1, 1};
                    auto channel = [level](float c) {
                        return static_cast<std::uint8_t>(std::clamp(128.0 * level * c, 0.0, 255.0));
                    };
                    pv.r = channel(base[0]);
                    pv.g = channel(base[1]);
                    pv.b = channel(base[2]);
                    remap[i] = static_cast<std::int32_t>(mesh.vertices.size());
                    mesh.vertices.push_back(pv);
                }
                mesh.indices.push_back(static_cast<std::uint32_t>(remap[i]));
            }
        }
        if (!mesh.indices.empty()) frame.prelit.push_back(std::move(mesh));
    }

    // The skeleton in picture pixels: each bone's origin to its parent's.
    v.bones.clear();
    if (v.skeleton) {
        const std::vector<anim::AnmBone>& ab = e.anm->bones();
        auto project = [&](std::size_t i, float* x, float* y) {
            const std::array<double, 3> p = to_view(cam, {static_cast<double>(world[i].t[0]), static_cast<double>(world[i].t[1]),
                                                          static_cast<double>(world[i].t[2])});
            if (p[2] < 1.0) return false;
            *x = static_cast<float>(frame.geom_ofs_x + cam.focal * p[0] / p[2]);
            *y = static_cast<float>(frame.geom_ofs_y + cam.focal * p[1] / p[2]);
            return true;
        };
        for (std::size_t i = 1; i < world.size() && i < ab.size(); ++i) {
            const int parent = ab[i].parent;
            // Bone 0 is the character's own place (the feet), not a joint.
            if (parent <= 0 || static_cast<std::size_t>(parent) >= world.size()) continue;
            float x0, y0, x1, y1;
            if (project(static_cast<std::size_t>(parent), &x0, &y0) && project(i, &x1, &y1)) v.bones.push_back({x0, y0, x1, y1});
        }
    }
    return !frame.prelit.empty();
}

bool build_library(Viewer& v, const scene::LibraryModel& lm, int w, int h, scene::Frame& frame) {
    const scene::CellMesh& mesh = lm.mesh;
    if (mesh.vertices.empty()) return false;
    std::array<double, 3> lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30};
    for (const scene::CellVertex& p : mesh.vertices) {
        const double q[3] = {p.x, p.y, p.z};
        for (std::size_t k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], q[k]);
            hi[k] = std::max(hi[k], q[k]);
        }
    }
    const std::array<double, 3> c{(lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5, (lo[2] + hi[2]) * 0.5};
    const double r = 0.5 * std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) +
                                     (hi[2] - lo[2]) * (hi[2] - lo[2]));
    const Camera cam = camera_for(v, c, r, h);
    frame.h = static_cast<float>(cam.focal);
    frame.geom_ofs_x = w * 0.5f;
    frame.geom_ofs_y = h * 0.5f;
    scene::ObjectRef o;
    // The store's own pointer, aliased: the scene renderer keys its GPU copy by the mesh.
    o.mesh = std::shared_ptr<const scene::CellMesh>(v.library, &v.library->mesh);
    o.rot = cam.rot;
    const std::array<double, 3> rc = mul(cam.rot, c);
    o.trans = {-rc[0], -rc[1], cam.distance - rc[2]};
    frame.objects.push_back(o);
    scene::Lighting& light = frame.light;
    light.tint = {128, 128, 128};
    light.fog = false;
    if (v.lit) {
        // Directional: the shader's normals point into the surface, so the light vector is the way the light goes.
        light.mode = 2;
        light.ambient = 0.42f * 4096.0f;
        light.light_vec = {-kToLight[0] * 0.7 * 4096.0, -kToLight[1] * 0.7 * 4096.0, -kToLight[2] * 0.7 * 4096.0};
    } else {
        light.mode = 0;
    }
    return true;
}

void request_picture(Viewer& v, int w, int h, std::string* why) {
    scene::Frame frame;
    bool ok = false;
    if (v.source == Source::Character) {
        CharaEntry* e = chara_entry(v.folder);
        if (e == nullptr) {
            *why = "reading the model...";
            return;
        }
        if (!e->model) {
            *why = e->error;
            return;
        }
        ok = build_character(v, *e, w, h, frame, why);
    } else if (v.source == Source::Library) {
        if (v.library_epoch != asset_epoch()) {
            v.library.reset();
            v.library_pending = false;
        }
        if (!v.library && !v.library_pending) {
            v.library_pending = true;
            const std::string file = v.file, name = v.name;
            defer([file, name] {
                Viewer& vs = viewer_state();
                if (vs.file != file || vs.name != name) return;
                vs.library = scene::find_model(file, name, false);
                vs.library_epoch = asset_epoch();
                vs.library_pending = false;
                if (!vs.library) vs.source = Source::None;
            });
        }
        if (!v.library) {
            *why = "reading the model...";
            return;
        }
        ok = build_library(v, *v.library, w, h, frame);
    }
    if (!ok) return;
    gpu::OffscreenScene job;
    job.frame = std::move(frame);
    job.width = w;
    job.height = h;
    job.wireframe = v.wireframe;
    gpu::request_offscreen_scene(std::move(job));
}

void draw_sources(Viewer& v) {
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##modelfilter", "filter characters and loaded models", v.filter, sizeof(v.filter));
    // Groups: everything, the characters, the loaded models, or the models of one folder.
    auto folder_of = [](const std::string& file) {
        const std::size_t cut = file.find_last_of('/');
        return cut == std::string::npos ? std::string("(root)") : file.substr(0, cut);
    };
    std::vector<std::string> folders;
    for (const scene::LoadedModel& lm : scene::loaded_models()) {
        const std::string f = folder_of(lm.file);
        if (std::find(folders.begin(), folders.end(), f) == folders.end()) folders.push_back(f);
    }
    std::sort(folders.begin(), folders.end());
    if (v.group != "all" && v.group != "chara" && v.group != "model" &&
        std::find(folders.begin(), folders.end(), v.group) == folders.end())
        v.group = "all";
    for (const char* g : {"all", "chara", "model"}) {
        if (ImGui::RadioButton(g, v.group == g)) v.group = g;
        ImGui::SameLine();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool in_folder = v.group != "all" && v.group != "chara" && v.group != "model";
    if (ImGui::BeginCombo("##modelfolder", in_folder ? v.group.c_str() : "model folder...")) {
        for (const std::string& f : folders)
            if (ImGui::Selectable(f.c_str(), v.group == f)) v.group = f;
        ImGui::EndCombo();
    }
    const bool show_chara = v.group == "all" || v.group == "chara";
    const bool show_models = v.group != "chara";
    // The list takes the height the rest of the panel leaves: a taller window shows more rows.
    const float row = ImGui::GetTextLineHeightWithSpacing();
    // The bottom edge can also be dragged.
    const float height = std::max(row * 10, ImGui::GetContentRegionAvail().y - v.below_list);
    if (ImGui::BeginChild("models", ImVec2(0, height), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY)) {
        auto matches = [&v](const std::string& s) {
            if (v.filter[0] == '\0') return true;
            std::string a = s, b = v.filter;
            for (auto* t : {&a, &b})
                std::transform(t->begin(), t->end(), t->begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return a.find(b) != std::string::npos;
        };
        if (const sh::chara::CharaLibrary* lib = show_chara ? Pc_CharaData_Library() : nullptr) {
            for (const sh::chara::CharacterDef& def : lib->characters()) {
                if (def.model.empty() || !matches(def.folder)) continue;
                const std::string label = "chara  " + def.folder + "##c" + def.folder;
                if (ImGui::Selectable(label.c_str(), v.source == Source::Character && v.folder == def.folder))
                    view_character(def.folder);
            }
        }
        int i = 0;
        for (const scene::LoadedModel& lm : scene::loaded_models()) {
            const std::string text = lm.model->name + "  (" + lm.file + ")";
            if (!show_models || (in_folder && folder_of(lm.file) != v.group) || !matches(text)) continue;
            const std::string label = "model  " + text + "##m" + std::to_string(i++);
            if (ImGui::Selectable(label.c_str(), v.source == Source::Library && v.file == lm.file && v.name == lm.model->name))
                view_library_model(lm.file, lm.model->name);
        }
    }
    ImGui::EndChild();
}

void draw_timeline(Viewer& v, CharaEntry& e) {
    if (!e.anm) return;
    ImGui::SeparatorText("Animation");
    ImGui::TextDisabled("%s: %zu keyframes, %zu clips", e.anm_path.c_str(), e.anm->keyframes().size(), e.clips.size());
    const std::string current = v.clip >= 0 && static_cast<std::size_t>(v.clip) < e.clips.size()
                                    ? e.clips[static_cast<std::size_t>(v.clip)].name
                                    : std::string("(whole file)");
    if (ImGui::BeginCombo("clip", current.c_str())) {
        if (ImGui::Selectable("(whole file)", v.clip < 0)) {
            v.clip = -1;
            v.keyframe = 0;
        }
        for (std::size_t i = 0; i < e.clips.size(); ++i) {
            const anim::Clip& c = e.clips[i];
            char label[160];
            std::snprintf(label, sizeof(label), "%s  [%d..%d]%s##%zu", c.name.c_str(), c.start, c.end, c.loop ? " loop" : "", i);
            if (ImGui::Selectable(label, v.clip == static_cast<int>(i))) {
                v.clip = static_cast<int>(i);
                v.keyframe = 0;
            }
        }
        ImGui::EndCombo();
    }
    int start = 0, end = 0;
    clip_range(e, v.clip, &start, &end);
    const float span = static_cast<float>(std::max(0, end - start));
    const double fps = v.clip >= 0 && static_cast<std::size_t>(v.clip) < e.clips.size() ? e.clips[static_cast<std::size_t>(v.clip)].fps : 30.0;
    if (v.playing && span > 0) {
        v.keyframe += static_cast<float>(ImGui::GetIO().DeltaTime * fps * v.speed);
        if (v.keyframe > span) v.keyframe = std::fmod(v.keyframe, span);
    }
    if (ImGui::Button(v.playing ? "Pause" : "Play")) v.playing = !v.playing;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    char fmt[48];
    std::snprintf(fmt, sizeof(fmt), "keyframe %d + %%.2f", start);
    if (ImGui::SliderFloat("##scrub", &v.keyframe, 0.0f, span, fmt)) v.playing = false;
    ImGui::SetNextItemWidth(120);
    ImGui::SliderFloat("speed", &v.speed, 0.1f, 2.0f, "%.2fx");
    ImGui::SameLine();
    ImGui::TextDisabled("%.0f keyframes/s", fps);
}

void draw_parts(Viewer& v, const sh::assets::SkinModel& model) {
    if (model.groups.empty() || !ImGui::TreeNode("Mesh nodes")) return;
    if (v.parts_off.size() != model.groups.size()) v.parts_off.assign(model.groups.size(), 0);
    for (std::size_t i = 0; i < model.groups.size(); ++i) {
        bool on = v.parts_off[i] == 0;
        char label[160];
        std::snprintf(label, sizeof(label), "%s  (material %d, %zu tris)##g%zu", model.groups[i].node.c_str(),
                      model.groups[i].material, model.groups[i].indices.size() / 3, i);
        if (ImGui::Checkbox(label, &on)) v.parts_off[i] = on ? 0 : 1;
    }
    ImGui::TreePop();
}

void draw_viewer_panel() {
    static bool s_placed = false;
    if (!s_placed) {
        // A first size and place, once: the host opens a user-sized panel with no height.
        s_placed = true;
        const ImVec2 screen = ImGui::GetMainViewport()->Size;
        const float scale = std::max(1.0f, screen.y / 720.0f);
        ImGui::SetWindowSize(ImVec2(std::min(560.0f * scale, screen.x * 0.62f), screen.y * 0.9f), ImGuiCond_Always);
        ImGui::SetWindowPos(ImVec2(screen.x - std::min(560.0f * scale, screen.x * 0.62f) - 8.0f, screen.y * 0.05f),
                            ImGuiCond_Always);
    }
    Viewer& v = viewer_state();
    if (ImGui::Button("Reload textures/models from disk")) reload_from_disk();
    draw_sources(v);
    // Measured to the end of the panel, for the list's height next frame.
    const float below_from = ImGui::GetCursorPosY();
    struct Measure {
        Viewer& v;
        float from;
        ~Measure() { v.below_list = ImGui::GetCursorPosY() - from; }
    } measure{v, below_from};
    if (v.source == Source::None) {
        ImGui::TextDisabled("Pick a character or a model (or click one in the world, then Open in model viewer).");
        return;
    }
    ImGui::Text("%s", v.source == Source::Character ? ("Character " + v.folder).c_str() : (v.name + "  " + v.file).c_str());
    ImGui::Checkbox("Lit", &v.lit);
    ImGui::SameLine();
    ImGui::Checkbox("Wireframe", &v.wireframe);
    if (v.source == Source::Character) {
        ImGui::SameLine();
        ImGui::Checkbox("Skeleton", &v.skeleton);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset view")) {
        v.yaw = 210.0f;
        v.pitch = -12.0f;
        v.zoom = 1.0f;
    }

    const float avail = std::max(ImGui::GetContentRegionAvail().x, 64.0f);
    const int w = std::clamp(static_cast<int>(avail), 64, 1024);
    const int h = std::clamp(static_cast<int>(avail * 0.62f), 48, 768);
    std::string why;
    request_picture(v, w, h, &why);
    std::vector<std::uint8_t> rgba;
    int pw = 0, ph = 0;
    if (gpu::take_offscreen_picture(&rgba, &pw, &ph)) {
        v.picture.set(pw, ph, rgba.data());
        v.picture_w = pw;
        v.picture_h = ph;
    }
    if (!why.empty()) ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s", why.c_str());
    const ImVec2 size(static_cast<float>(w), static_cast<float>(h));
    if (v.picture.valid()) {
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::Image(ImTextureRef(v.picture.id()), size);
        if (ImGui::IsItemHovered()) {
            const ImGuiIO& io = ImGui::GetIO();
            if (io.MouseWheel != 0.0f) v.zoom = std::clamp(v.zoom * (io.MouseWheel > 0 ? 1.1f : 1.0f / 1.1f), 0.1f, 20.0f);
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            v.yaw -= d.x * 0.5f;
            v.pitch = std::clamp(v.pitch - d.y * 0.5f, -89.0f, 89.0f);
        }
        if (v.skeleton && v.picture_w > 0) {
            ImDrawList* list = ImGui::GetWindowDrawList();
            const float sx = size.x / static_cast<float>(v.picture_w), sy = size.y / static_cast<float>(v.picture_h);
            for (const auto& b : v.bones) {
                const ImVec2 a(p0.x + b[0] * sx, p0.y + b[1] * sy), c(p0.x + b[2] * sx, p0.y + b[3] * sy);
                list->AddLine(a, c, IM_COL32(80, 255, 120, 255), 2.0f);
                list->AddCircleFilled(c, 2.5f, IM_COL32(255, 230, 60, 255));
            }
        }
    } else {
        ImGui::Dummy(size);
    }
    ImGui::TextDisabled("drag: orbit   wheel: zoom");
    ImGui::SetNextItemWidth(160);
    ImGui::SliderFloat("yaw", &v.yaw, 0.0f, 360.0f, "%.0f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    ImGui::SliderFloat("pitch", &v.pitch, -89.0f, 89.0f, "%.0f");

    if (v.source == Source::Character) {
        if (CharaEntry* e = chara_entry(v.folder); e != nullptr && e->model) {
            ImGui::TextDisabled("%s", e->host.c_str());
            draw_timeline(v, *e);
            draw_parts(v, *e->model);
        }
    } else if (v.library) {
        const scene::CellMesh& m = v.library->mesh;
        ImGui::TextDisabled("%zu vertices, %zu triangles, %zu materials", m.vertices.size(), m.indices.size() / 3,
                            m.materials.size());
        if (ImGui::TreeNode("Materials")) {
            for (std::size_t i = 0; i < m.materials.size(); ++i) {
                const std::string& t = m.materials[i].texture;
                ImGui::PushID(static_cast<int>(i));
                if (!t.empty() && ImGui::SmallButton("Browse")) browse_texture(t, m.materials[i].palette);
                ImGui::SameLine();
                ImGui::TextUnformatted(t.empty() ? "(untextured)" : t.c_str());
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
    }
}

void register_model_commands() {
    using sh::console::ArgSpec;
    using sh::console::ArgType;
    sh::console::CommandSpec cmd;
    cmd.name = "editormodel";
    cmd.group = "debug";
    cmd.help = "drive the editor's model viewer: chara <folder> | model <file> <name> | clip <index> [keyframe] | "
               "view <yaw> <pitch> [zoom] | opt <lit|wireframe|skeleton|play> <0|1>";
    cmd.args = {ArgSpec{"what", ArgType::Enum, false, "what to set", {"chara", "model", "clip", "view", "opt"}, {}},
                ArgSpec{"a", ArgType::String, false, "", {}, {}}, ArgSpec{"b", ArgType::String, true, "", {}, {}},
                ArgSpec{"c", ArgType::String, true, "", {}, {}}};
    cmd.run = [](sh::console::Context& ctx, const sh::console::Args& args) -> Result<void> {
        Viewer& v = viewer_state();
        const std::string& what = args.str(0);
        auto num = [&args](std::size_t i, double fallback) {
            return args.has(i) ? std::strtod(args.str(i).c_str(), nullptr) : fallback;
        };
        if (what == "chara") {
            view_character(args.str(1));
        } else if (what == "model") {
            view_library_model(args.str(1), args.str(2));
        } else if (what == "clip") {
            v.clip = static_cast<int>(num(1, -1));
            v.keyframe = static_cast<float>(num(2, 0));
            if (args.has(2)) v.playing = false;
        } else if (what == "view") {
            v.yaw = static_cast<float>(num(1, v.yaw));
            v.pitch = static_cast<float>(num(2, v.pitch));
            v.zoom = static_cast<float>(num(3, v.zoom));
        } else {
            const bool on = num(2, 1) != 0.0;
            const std::string& o = args.str(1);
            if (o == "lit") v.lit = on;
            else if (o == "wireframe") v.wireframe = on;
            else if (o == "skeleton") v.skeleton = on;
            else if (o == "play") v.playing = on;
            else ctx.out.error("editormodel opt: lit, wireframe, skeleton or play");
        }
        return Result<void>::ok();
    };
    (void)sh::console::global_registry().add(std::move(cmd));
}

struct Install {
    Install() {
        Panel p;
        p.id = "tools.models";
        p.title = "Model viewer";
        p.menu = "Assets";
        p.draw = &draw_viewer_panel;
        p.open = false;
        p.auto_size = false;
        register_panel(std::move(p));
        register_model_commands();
    }
};

const Install s_install;

}  // namespace

const sh::assets::SkinModel* character_model(const std::string& folder, std::string* error) {
    CharaEntry* e = chara_entry(folder);
    if (e == nullptr) return nullptr;
    if (error) *error = e->error;
    return e->model.get();
}

const Picture* character_texture(const std::string& folder, int texture) {
    CharaEntry* e = chara_entry(folder);
    if (e == nullptr || texture < 0 || static_cast<std::size_t>(texture) >= e->pictures.size()) return nullptr;
    return e->pictures[static_cast<std::size_t>(texture)].get();
}

std::string character_model_file(const std::string& folder) {
    CharaEntry* e = chara_entry(folder);
    return e != nullptr ? e->host : std::string();
}

void view_character(const std::string& folder) {
    Viewer& v = viewer_state();
    if (v.source != Source::Character || v.folder != folder) {
        v.clip = -1;
        v.keyframe = 0;
        v.parts_off.clear();
    }
    v.source = Source::Character;
    v.folder = folder;
    run_command("editorpanel tools.models on");
}

void view_library_model(const std::string& file, const std::string& name) {
    Viewer& v = viewer_state();
    v.source = Source::Library;
    if (v.file != file || v.name != name) {
        v.library.reset();
        v.library_pending = false;
    }
    v.file = file;
    v.name = name;
    run_command("editorpanel tools.models on");
}

}  // namespace sh::tools::assets
