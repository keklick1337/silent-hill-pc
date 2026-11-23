#include "render/world/world_scene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "engine/assets/texture_upscale.h"
#include "engine/effects/named_effects_host.h"
#include "engine/vfs/vfs_c.h"
#include "engine/world/world_atmosphere.h"
#include "formats/png/palette_variant.h"
#include "render/frame/frame.h"
#include "render/scene/scene.h"
#include "render/scene/scene_c.h"
#include "render/shaderstate/shaderstate.h"
#include "render/world/surface_material_files.h"
#include "render/world/world_scene_source.h"

namespace sh::world_scene {

namespace {

// The world's ordering table has 2^11 slots; the map cells go into the farthest one.
constexpr std::uint32_t kWorldSlots = 2048;

std::optional<std::vector<std::uint8_t>> read_gamedata_file(const std::string& relative) {
    char path[1024];
    if (!ShVfs_FindDataFile(relative.c_str(), path, sizeof(path))) return std::nullopt;
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) return std::nullopt;
    std::vector<std::uint8_t> bytes;
    std::uint8_t chunk[65536];
    std::size_t n;
    while ((n = std::fread(chunk, 1, sizeof(chunk), f)) > 0) bytes.insert(bytes.end(), chunk, chunk + n);
    std::fclose(f);
    return bytes;
}

void ensure_reader() {
    static bool s_init = false;
    if (!s_init) {
        s_init = true;
        // A palette variant ("X.p03.png") is built from X.png and X.tim.json when no file has that name.
        // The paths whose last read gave an upscaled picture: those textures get a mip chain.
        static std::set<std::string, std::less<>> s_upscaled;
        scene::set_file_reader([](std::string_view relative) {
            auto bytes = png::read_texture_or_variant(std::string(relative), read_gamedata_file);
            if (bytes) {
                if (auto upscaled = sh::upscale::find(relative, *bytes)) {
                    s_upscaled.emplace(relative);
                    return upscaled;
                }
            }
            if (auto it = s_upscaled.find(relative); it != s_upscaled.end()) s_upscaled.erase(it);
            return bytes;
        });
        scene::set_mipmap_query([](std::string_view relative) { return s_upscaled.count(relative) != 0; });
        scene::set_mod_file_query([](std::string_view relative) {
            char path[1024];
            return ShVfs_FindModDataFile(std::string(relative).c_str(), path, sizeof(path)) != 0;
        });
        // The import's own picture: a PNG's IHDR (width and height, big-endian, at bytes 16 and 20).
        scene::set_picture_size_query([](const std::string& path, int* w, int* h) {
            const auto bytes = png::read_texture_or_variant(path, read_gamedata_file);
            if (!bytes || bytes->size() < 24) return false;
            auto be32 = [&](std::size_t o) {
                return static_cast<int>(static_cast<std::uint32_t>((*bytes)[o]) << 24 | (*bytes)[o + 1] << 16 |
                                        (*bytes)[o + 2] << 8 | (*bytes)[o + 3]);
            };
            *w = be32(16);
            *h = be32(20);
            return *w > 0 && *h > 0;
        });
        scene::set_material_source([](std::vector<std::string>* errors) {
            return world::load_surface_materials(effects::named::gamedata_dir(), effects::named::mods_dir(), errors);
        });
    }
}

void describe_camera(const source::Camera& cam) {
    double rot[9], trans[3];
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) rot[r * 3 + c] = cam.m[r][c];
        trans[r] = cam.t[r];
    }
    ShScene_SetCamera(rot, trans, cam.h, cam.ofs_x, cam.ofs_y);
}

// The map light in the units scene.h describes (RENDER.md section 6).
void describe_lighting(const source::Camera& cam) {
    const world::AtmosphereState& a = world::atmosphere().state();
    ShSceneLighting l;
    std::memset(&l, 0, sizeof(l));
    const float tint[3] = {static_cast<float>(a.world_tint.r), static_cast<float>(a.world_tint.g),
                           static_cast<float>(a.world_tint.b)};
    l.mode = a.light_mode;
    for (int i = 0; i < 3; i++) {
        const float far = static_cast<float>(cam.far_color[i] >> 4);
        l.farColor[i] = far;
        // Without a light the tint goes toward the far colour by the darkness.
        l.tint[i] = l.mode == 0 ? std::floor(tint[i] + (far - tint[i]) * (4096 - a.ambient) / 4096.0f) : tint[i];
    }
    l.ambient = static_cast<float>(a.ambient);
    l.maxIntensity = static_cast<float>(a.light_level_max);
    l.depthCue = static_cast<float>(a.light_depth_cue);
    l.fog = a.fog_enabled ? 1 : 0;
    const double alpha = a.light_intensity / 4096.0;
    l.lightVec[0] = a.light_direction.x * alpha;
    l.lightVec[1] = a.light_direction.y * alpha;
    l.lightVec[2] = a.light_direction.z * alpha;
    // Q12 to Q8, rounding toward minus infinity as the game's shift does.
    l.lightPos[0] = static_cast<double>(a.light_position.x >> 4);
    l.lightPos[1] = static_cast<double>(a.light_position.y >> 4);
    l.lightPos[2] = static_cast<double>(a.light_position.z >> 4);
    ShScene_SetLighting(&l);
}

// The area file of a cell ("ER0402" -> ER.glb) or of a global library ("THR_GLB" -> THR.glb): the stem without its
// last four characters.
std::string area_file(const std::string& stem) {
    return "maps/areas/" + (stem.size() > 4 ? stem.substr(0, stem.size() - 4) : stem) + ".glb";
}

// The ordering-table bucket base of a model's group.
std::uint32_t group_base(int group) {
    static constexpr std::uint32_t kBase[6] = {2, 0, 4, 33, 66, 99};
    return group >= 0 && group < 6 ? kBase[group] : kBase[0];
}

// Poses the library model in the world and puts its scene entry where the PS1 mesh drawer put the model's prims: the
// group's base bucket plus the view depth of the model's origin over 2^(shift + 2).
bool draw_model(const std::string& file, const std::string& name, const Pose& pose, int shift, double scale = 1.0) {
    double rot[9], trans[3];
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) rot[r * 3 + c] = pose.m[r][c] / 4096.0 * scale;
        trans[r] = static_cast<double>(pose.t[r]);
    }
    int group = 0;
    const unsigned code = ShScene_AddObject(file.c_str(), name.c_str(), rot, trans, &group);
    if (code == 0) return false;
    // The frame's camera and light now: the world draw may not have run yet this frame (or ran without cells).
    const source::Camera cam = source::camera();
    describe_camera(cam);
    describe_lighting(cam);
    double z = cam.t[2];
    for (int c = 0; c < 3; c++) z += cam.m[2][c] * trans[c];
    const std::uint32_t depth = static_cast<std::uint32_t>(std::max(0, static_cast<int>(z)) >> (shift + 2));
    const std::uint32_t slot = std::min(group_base(group) + depth, kWorldSlots - 1);
    frame::submit_keyed(frame::Kind::Scene, code, {frame::Layer::World, slot});
    return true;
}

}  // namespace

bool draw_world(int shift) {
    ensure_reader();
    if (!source::global_library_ready()) return true;

    const source::Camera cam = source::camera();
    describe_camera(cam);
    describe_lighting(cam);
    static std::vector<source::Cell> cells;
    cells.clear();
    source::drawable_cells(cells);
    if (!cells.empty()) {
        // The world's fog ramp, its cull depth and the bucket depth scale for the frame's later prims and the
        // scene's fog, as the PS1 mesh drawer left them.
        const world::AtmosphereState& a = world::atmosphere().state();
        source::set_bucket_depth_shift(shift + 2);
        render::set_world_fog(a.fog_ramp.data(), a.fog.depth_shift, a.fog.intensity, a.fog_enabled != 0,
                              source::world_far_limit(shift));
    }
    for (const source::Cell& c : cells) {
        if (c.stem.empty()) continue;
        ShScene_AddCell(c.stem.c_str(), c.cell_x, c.cell_z);
        source::draw_cell_glows(c.slot, shift);
    }
    frame::submit_keyed(frame::Kind::Scene, 0, {frame::Layer::World, kWorldSlots - 1});
    return true;
}

namespace {

// The library file and model path of a world object of LM slot `library` (1 the item library, 2 the map's global
// library, 3 + n the n-th resident cell); false when the slot names no library.
bool object_model(int library, std::string_view name, std::string& file, std::string& model) {
    std::string root;
    if (library == 1) {
        root = "BG_ITEM";
        file = "maps/areas/BG_ITEM.glb";
    } else if (library == 2) {
        root = source::global_library_stem();
        file = area_file(root);
    } else if (library >= 3) {
        const std::optional<std::string> stem = source::resident_cell_stem(library - 3);
        if (!stem) return false;
        root = *stem;
        file = area_file(root);
    }
    if (root.empty()) return false;
    model = root + "/" + std::string(name.substr(0, std::min<std::size_t>(name.size(), 8)));
    return true;
}

}  // namespace

bool draw_object(int library, std::string_view name, const Pose& pose, int shift) {
    if (name.empty()) return false;
    ensure_reader();
    std::string file, model;
    return object_model(library, name, file, model) && draw_model(file, model, pose, shift);
}

bool has_object(int library, std::string_view name) {
    if (name.empty()) return false;
    ensure_reader();
    std::string file, model;
    return object_model(library, name, file, model) && sh::scene::find_model(file, model, false) != nullptr;
}

bool draw_held_item(std::string_view model_file, const Pose& pose, int shift) {
    if (model_file.empty()) return false;
    ensure_reader();
    std::string path(model_file);
    if (const std::size_t dot = path.rfind('.'); dot != std::string::npos) path.resize(dot);
    const std::size_t slash = path.rfind('/');
    const std::string stem = slash == std::string::npos ? path : path.substr(slash + 1);
    return draw_model("convert/models/" + path + ".glb", stem + "/#0", pose, shift);
}

bool draw_item_model(std::string_view model_file, const Pose& pose, double scale, int shift) {
    if (model_file.empty()) return false;
    ensure_reader();
    std::string path(model_file);
    if (const std::size_t dot = path.rfind('.'); dot != std::string::npos) path.resize(dot);
    const std::size_t slash = path.rfind('/');
    const std::string stem = slash == std::string::npos ? path : path.substr(slash + 1);
    const std::string file = "convert/models/" + path + ".glb";
    // A model library names its models by index; the glb of a TMD has plain object nodes.
    const bool indexed = sh::scene::find_model(file, stem + "/#0", false) != nullptr;
    return draw_model(file, stem + (indexed ? "/#0" : "/OBJ0"), pose, shift, scale);
}

}  // namespace sh::world_scene
