#pragma once

// The engine's scene renderer (RENDER.md section 6): the 3D world drawn from the unified map files
// (maps/areas/<AREA>.glb) and the characters from their skinned glb, through the RHI.
// Engine types only: the game describes each frame through the C face (scene_c.h), and the PS1
// frame state the scene has to match (projection, fog, flashlight) comes from the GPU renderer (render/gpu) at the
// point of the frame where the scene is drawn.
//
// Space: everything is in the PS1 view space the game draws in (x right, y down, z forward, 256 units per
// metre), so the fog ramp, the flashlight and the depth buffer shared with the PS1 prims drawn on top keep
// their meaning.

#include "formats/common/status.h"
#include "render/rhi/prim_batch.h"
#include "render/rhi/rhi.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace sh::rhi {
struct RayScene;
}

namespace sh::scene {

// How far behind its own plane, along the normal, a map surface is written into the depth buffer: one slot of the
// PS1 ordering table (8 view units, about 3 cm). The original sorted a polygon into the slot of its farthest corner
// and linked the world objects before the map, so within a slot an object was drawn over the map surface it is
// mounted on; some are authored a few units inside their wall (the hospital's map board: 3 units). Measured along
// the normal the margin is the same at every view angle: a foot, an item, a mark or a wall-mounted prop within it
// wins, while a corner, a ledge or a wall farther in front of a prop than that still hides it. A 3D prim that has to
// meet the map at its true place adds it to its own depth.
inline constexpr float kWorldDepthBias = 8.0f;

// World (PS1 axes, 256 per metre) to view: v = rot * p + trans, rot row-major and unit scale.
struct Camera {
    std::array<double, 9> rot{1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::array<double, 3> trans{0, 0, 0};
};

// The world light of the frame, in the units the map data was authored for (section 11.3).
struct Lighting {
    int mode = 0;                                  // 0 flat tint, 1 point light, 2 directional light
    std::array<float, 3> tint{128, 128, 128};      // 0..255; mode 0: the final flat colour
    std::array<float, 3> far_color{0, 0, 0};       // 0..255, what a lit vertex fades to without fog
    float ambient = 0;                             // added to every lit intensity (Q12 scale)
    float max_intensity = 255;                     // lit intensity ceiling
    float depth_cue = 0;                           // distance falloff factor of the point light
    bool fog = false;
    std::array<double, 3> light_vec{0, 0, 0};      // world axes: direction times intensity (Q12)
    std::array<double, 3> light_pos{0, 0, 0};      // world position
};

// A light of the map's lights.json (RENDER.md section 10) as the frame shows it: resolved by the engine (its
// flag conditions, the bone it follows, the effect it waits for), in world units (PS1 axes, 256 per metre). Only the
// modern lighting (rt_lights) reads it; the original image has no such term.
struct FrameLight {
    std::array<float, 3> position{0, 0, 0};
    std::array<float, 3> direction{0, 0, 0};  // a spot's unit axis (world); zero for a point light
    std::array<float, 3> color{0, 0, 0};      // linear, times the intensity
    float range = 1024;                       // the light is gone at this distance
    float cos_outer = -1;                     // a spot's cone: outer and inner cosines
    float cos_inner = -1;
    float radius = 8;     // the emitter's size: the softness of its ray-traced shadows
    float near_clip = 0;  // shadow rays stop this short of it (the hand that holds it)
    bool casts_shadows = true;
    // Without ray tracing it may take the flashlight's shadow map while the flashlight is off (lights.json
    // "shadow_map"); the map looks along shadow_axis (world, unit), a spot along its own axis.
    bool shadow_map = false;
    std::array<float, 3> shadow_axis{0, 1, 0};
    bool gi = true;  // it bounces (rt_gi)
    // A lamp of the extra_lights setting: drawn by every renderer, also where the map's other lights are off.
    bool extra = false;
    // A light a script gave a character to carry (sh.light.carry): part of the story, so it is shaded by every
    // renderer whatever the light settings are.
    bool carried = false;
};

// The map's sun and sky for the modern lighting, daytime maps only (lights.json "sun" and "sky").
struct FrameSun {
    bool on = false;
    std::array<float, 3> direction{0, 1, 0};  // world, unit, the way the light travels
    std::array<float, 3> color{0, 0, 0};      // linear, times the intensity
    float radius = 0.01f;                     // angular radius (radians): the softness of its shadows
    std::array<float, 3> sky{0, 0, 0};        // the sky's light on surfaces open to it, linear
    // Roofs the cells do not draw (lights.json "ceilings"): world units, x0, z0, x1, z1 and the height y.
    std::vector<std::array<float, 5>> ceilings;
};

struct CellRef {
    std::string stem;  // "ER0402"
    int cell_x = 0;    // where the game placed the cell (40 m grid)
    int cell_z = 0;
};

struct PrelitVertex {
    float x = 0, y = 0, z = 0;  // view space
    float u = 0, v = 0;
    std::uint8_t r = 128, g = 128, b = 128, a = 255;  // 128 = unmodulated
    // The triangle's UV box (normalised, as CellVertex::box_*): texels lo .. hi - 1 are shown; hi < lo = none.
    float box_u0 = 0, box_v0 = 0, box_u1 = -1, box_v1 = -1;
};

enum class Blend : std::uint8_t {
    Opaque,       // alpha 0 texels are holes
    Additive,     // PS1 semi-transparency mode 1: texels with the STP bit add, the others are solid
    Average,      // alpha blend by texel alpha
    Mean,         // PS1 mode 0: STP texels half over half (B / 2 + F / 2), the others solid
    Subtractive,  // PS1 mode 2: STP texels subtract (B - F); STP texels only (the solid ones need their own pass)
    AddQuarter,   // PS1 mode 3: a quarter of STP texels adds (B + F / 4); STP texels only, like Subtractive
};
inline constexpr int kBlendCount = 6;

// The scene's blend of a PS1 semi-transparency mode (ABR 0..3).
constexpr Blend blend_of_abr(int abr) {
    switch (abr & 3) {
        case 0:
            return Blend::Mean;
        case 1:
            return Blend::Additive;
        case 2:
            return Blend::Subtractive;
        default:
            return Blend::AddQuarter;
    }
}

// A mesh posed and lit by the game this frame (a character, an item preview): positions already in view space.
struct PrelitMesh {
    std::uint64_t texture_key = 0;            // 0 = untextured; see prelit_texture()
    Blend blend = Blend::Opaque;
    // PS1 semi-transparency in two passes: the solid texels with the opaque surfaces (writing depth), then the STP
    // texels with `blend`. Without it the mesh is one draw with `blend` (Subtractive and AddQuarter need it).
    bool split_stp = false;
    // Every texel counts as STP (the game set the semi-transparency bit of the whole palette): one blended pass.
    bool all_stp = false;
    float fade = 0;                           // 0..1: how much of the flashlight the mesh does not take
    bool casts_shadow = true;                 // into the flashlight shadow map
    // An isolated model (the item previews of the inventory and the pickup screen): its own projection (the item
    // camera, not the world's), and neither the world fog nor the flashlight reach it.
    bool isolated = false;
    float h = 0;                              // isolated: projection distance and centre (screen units)
    float geom_ofs_x = 0;
    float geom_ofs_y = 0;
    // Isolated: no colour, only depth written over whatever is there (a UI preview's own depth, preview.h).
    bool depth_reset = false;
    std::vector<PrelitVertex> vertices;
    std::vector<std::uint32_t> indices;
    // Model upscaling (model_upscale.h): the patch normal of every vertex (view space, w = 1 on a crease), filled by
    // the pose code while upscaling is on; empty = drawn plain. The meshes of one model share `upscale_model` (not 0)
    // and so one level, which keeps their shared edges closed.
    std::vector<std::array<float, 4>> patch_normals;
    std::uint32_t upscale_model = 0;
};

struct CellMesh;

// A library model posed in the world this frame (a world object, the held item): drawn like a cell, lit by the map's
// light, from its own mesh in model space.
struct ObjectRef {
    std::shared_ptr<const CellMesh> mesh;
    std::array<double, 9> rot{1, 0, 0, 0, 1, 0, 0, 0, 1};  // model to world, row-major; may carry one scale
    std::array<double, 3> trans{0, 0, 0};                  // world position (PS1 units)
};

// The scene item of frame.objects[i] is kObjectItem + i (cells are -1, posed meshes 0 .. prelit.size() - 1).
inline constexpr int kObjectItem = 0x20000000;

// What the game asked to be drawn this frame.
struct Frame {
    std::uint64_t serial = 0;    // counts frames: a renderer prepares each frame once
    bool world = false;          // the world was submitted (a scene point is in the frame)
    Camera camera;
    float h = 256;               // projection distance (screen units)
    float geom_ofs_x = 0;        // projection centre (screen units, before the draw-area offset)
    float geom_ofs_y = 0;
    Lighting light;
    std::vector<CellRef> cells;
    std::vector<PrelitMesh> prelit;
    std::vector<ObjectRef> objects;
    // The lamp whose glow lies mirrored on the water surface this frame (world units), and the glow's strength 0..1.
    std::array<float, 3> water_lamp{0, 0, 0};
    float water_glow = 0;
    // The lamp glows of the drawn cells (the original's halo sprites): world xyz, kind. The map lights follow them.
    std::vector<std::array<float, 4>> lamp_glows;
    std::vector<FrameLight> lights;
    FrameSun sun;
    // The falling snow and ash flakes drawn this frame: view xyz and radius (view units). Only particle_shadows
    // reads them.
    std::vector<std::array<float, 4>> flakes;
};

inline constexpr std::size_t kSceneLights = 32;
inline constexpr std::size_t kSceneFlakes = 64;
inline constexpr std::size_t kFlakeGrid = 16;    // scene.frag FlakeShadowSun
inline constexpr std::size_t kFlakeList = 1024;
inline constexpr std::size_t kLightTilesX = 16;  // scene.frag MapLights: the picture's tiles across
inline constexpr std::size_t kLightTilesY = 8;   // and down
inline constexpr std::size_t kLightTiles = kLightTilesX * kLightTilesY;

// The map's lights and sun shaded without ray tracing (map_lights), the sun's shadow map (sun_shadows) and the
// flakes' shadow term (particle_shadows), in view space; std140, after the water's block in scene.frag. All zero:
// nothing is added and the image is the one without them.
struct LightBlock {
    std::array<float, 4> info{};         // x: lights, y: sun on, z: sun shadow map on, w: flakes
    std::array<float, 4> sun_dir{};      // xyz: the way the sunlight travels, w: the shadow map's texel (view units)
    std::array<float, 4> sun_color{};    // rgb, w: angular radius (radians)
    std::array<float, 4> sun_map{};      // x: map side (texels), y: depth range (view units), z: flake opacity,
                                         // w: 1 the flakes also shade the flashlight
    std::array<float, 16> sun_matrix{};  // view space to the sun map's clip space (GL convention, rows as written)
    std::array<float, 4> shadow{};       // x: 1 + the light shaded through the flashlight's shadow map, 0: none,
                                         // y: the sun shadow map's taps
    std::array<std::array<float, 4>, kSceneLights> pos{};     // xyz, w: range
    std::array<std::array<float, 4>, kSceneLights> dir{};     // a spot's axis (zero: point), w: outer cone cosine
    std::array<std::array<float, 4>, kSceneLights> color{};   // rgb, w: inner cone cosine
    std::array<std::array<float, 4>, kSceneFlakes> flakes{};  // xyz, w: radius
    // The flakes binned by where their sun shadows can fall (scene.frag FlakeShadowSun): kFlakeGrid x kFlakeGrid cells
    // in the plane across the sunlight, each with the flakes whose shadow can reach it, in rising order.
    std::array<float, 4> flake_grid{};  // xy: the grid's corner (u, v), z: cells per view unit, w: 1 = grid on
    std::array<float, 4> flake_u{};     // xyz: u axis, w: the largest squared distance from the eye it holds for
    std::array<float, 4> flake_v{};     // xyz: v axis, w: the lowest point along the light it holds for
    std::array<std::array<float, 4>, kFlakeGrid * kFlakeGrid / 2> flake_cells{};  // two cells per entry: first, count
    std::array<std::array<float, 4>, kFlakeList / 4> flake_list{};               // flake indices, four per entry
    // The lights that can reach each tile of a kLightTilesX x kLightTilesY grid over the picture (scene.frag
    // MapLights): bit i of a tile's mask is light i, four tiles per entry. The scene renderer fills it from pos.
    std::array<std::array<std::uint32_t, 4>, kLightTiles / 4> tiles{};
};

// The PS1 frame state at the scene point: what the PS1 prims around the scene are drawn with.
struct View {
    std::array<float, 16> ortho{};  // screen units to clip space
    float draw_ofs_x = 0;           // draw-area offset added to screen positions
    float draw_ofs_y = 0;
    float sz_max = 65535;           // view depth that maps to the far end of the depth buffer
    float pixel_w = 0;              // size of the viewport in pixels (0 = unknown): the seam lines' one-pixel shifts
    float pixel_h = 0;
    rhi::PrimFsUniforms fs;          // fog colour and strength, soft fog, filter, dither, flashlight
    rhi::FogRampUniforms fog;        // the fog ramp
    // `fog`'s ramp as the fragment shader fetches it (PrimRenderer::fogRamp, uploaded with the same ramp). None: a
    // black texel, which serves only a draw without fog (params[0] off).
    rhi::TextureH fog_ramp;
    rhi::TextureH shadow_map;       // flashlight shadow depth (fs.prog[3] on)
    rhi::SamplerH shadow_sampler;   // the map's sampler; SDL3 GPU drops a binding without one
    rhi::TextureH shadow_tint;      // what translucent surfaces let through the same map; none: nothing is tinted
    // Ray-traced results of the frame (rhi_vulkan_rt.h) at the target's pixels, rgb and the view z traced there; a
    // surface takes them only where its own z matches. None: that effect is off for this draw.
    rhi::TextureH rt_shadow;   // the flashlight's light after the shadow rays (replaces the shadow map)
    rhi::TextureH rt_reflect;  // reflected light, already weighed by the surface
    bool rt_reflect_mirrors = false;  // rt_reflect traced mirror rays (rt_reflections), not only light highlights
    rhi::TextureH rt_gi;       // the beam's bounce, times the surface's albedo
    LightBlock lights;
    rhi::TextureH sun_shadow;  // the sun's shadow depth (lights.info.z on)
    rhi::SamplerH sun_sampler;
};

// What the editor's viewer changes in the scene's look while it is open (src/features/tools).
// Inactive by default: the game's frames never see it. Applied per draw, so nothing of the game's light or fog is
// written.
struct ViewerOverrides {
    bool active = false;
    bool wireframe = false;       // triangles as edges
    bool full_bright = false;     // flat light at light_scale, no fog, no flashlight; posed meshes too
    float light_scale = 1.0f;     // the map light's level (1 = as the game lights it)
    bool fog_off = false;         // no fog ramp, no soft fog wall
    bool flashlight_off = false;  // no per-pixel flashlight; the point light becomes the room's darkness
    // The texture browser's "highlight in world": cell and object materials whose picture is this gamedata path
    // ("convert/textures/BG/X.p09.png") are drawn in a flat highlight colour instead. Empty: none.
    std::string highlight_texture;
};
ViewerOverrides& viewer_overrides();

// Reads a gamedata file by relative path ("maps/areas/ER.glb"), mods first. Empty when there is none.
using FileReader = std::function<std::optional<std::vector<std::uint8_t>>(std::string_view relative)>;
using LogFn = std::function<void(int level, const std::string& message)>;

struct SceneTexture;
struct LibraryModel;

// The frame being described by the game (one per process; the game is single threaded).
Frame& current_frame();
// The cells and world objects of the last frame that drew the world: what stands in the world between frames.
const std::vector<CellRef>& placed_cells();
const std::vector<ObjectRef>& placed_objects();
void set_file_reader(FileReader reader);
// True when the picture the reader returned last for `relative` is an upscaled one (texture_upscale): such a
// texture gets a mip chain. Without a query no texture has one.
using MipmapQuery = std::function<bool(std::string_view relative)>;
void set_mipmap_query(MipmapQuery query);
// True when the reader takes `relative` from a mod: its meshes are drawn as the mod made them (CellMesh::from_mod).
// Without a query every file is the game's.
using ModFileQuery = std::function<bool(std::string_view relative)>;
void set_mod_file_query(ModFileQuery query);
// A gamedata file through the reader (mods first); empty without a reader or the file.
std::optional<std::vector<std::uint8_t>> read_scene_file(std::string_view relative);
void set_log(LogFn log);
void log(int level, const std::string& message);

// A cell of the area files by stem, loading its area on first use; null when no area file has it (logged once).
std::shared_ptr<const CellMesh> find_cell(std::string_view stem);
// A library model by gamedata file ("maps/areas/ER.glb", "convert/models/ITEM/HANDGUN.glb") and name ("ER0102/DOOR_HID",
// or "<ROOT>/#<index>" by model index), the file read on first use; null when the file has no such model (logged
// once unless `log_missing` is false: a lookup that only asks whether the file has it).
std::shared_ptr<const LibraryModel> find_model(std::string_view file, std::string_view name, bool log_missing = true);
// A map texture by gamedata path ("convert/textures/BG/X.p09.png") with its TIM palette for the STP bit; decoded on
// first use. Null when it cannot be read (logged once).
std::shared_ptr<const SceneTexture> find_texture(const std::string& path, int palette);
// The textures of `wanted` that are not loaded yet, decoded (with their mip chains) on several threads: a map's first
// frame would otherwise decode its upscaled pictures one after another, seconds of a frozen frame. find_texture then
// finds them.
void prefetch_textures(std::span<const std::pair<std::string, int>> wanted);
// Texture of posed meshes, registered with the first mesh that used the key.
void add_prelit_texture(std::uint64_t key, int width, int height, const std::uint8_t* rgba);
std::shared_ptr<const SceneTexture> prelit_texture(std::uint64_t key);
// A better picture of loaded textures arrived (texture_upscale): the map textures whose path `match` accepts are read
// again at their next use and the posed-mesh texture `key` is registered again by its next mesh, texture by texture,
// without forget_assets(). The renderer drops its copies of them (take_refreshed_textures).
void refresh_textures(const std::function<bool(const std::string& path)>& match);
void refresh_prelit_texture(std::uint64_t key);
struct RefreshedTextures {
    std::vector<std::pair<std::string, int>> textures;  // (path, palette) as find_texture() names them
    std::vector<std::uint64_t> prelit;
    bool empty() const { return textures.empty() && prelit.empty(); }
};
// What was refreshed since the last call.
RefreshedTextures take_refreshed_textures();
// The water surface (gamedata/effects/water.json "surface"): cells' material of this name has no picture and is
// drawn by the water shader of scene.frag: a dark base, the frame's light on a moving wave pattern, the posed meshes
// mirrored in the surface's plane and the lamp's mirrored glow. Colours 0..1, lengths in metres.
// One quality level of the surface (render::graphics().water_quality picks it).
struct WaterQuality {
    int wave_layers = 3;           // 1..3 wave sets summed
    float wave_grid = 0.0f;        // squares per metre the waves are evaluated on; 0: every pixel
    int color_steps = 0;           // levels the wave's height and the glow are stepped to; 0: smooth
    int phase_steps = 0;           // steps of one wave cycle; 0: every frame
    float distortion = 1.0f;       // 0..1 of wave_distortion
    int reflection_lines = 240;    // rows of the mirrored picture; 0: no mirrored pass
    bool reflection_smooth = true; // linear sampling and the blur; else the picture's own pixels
    int blur_taps = 5;             // 1, 3 or 5
};
struct WaterSurface {
    std::string material;                         // empty: no material is water
    std::array<float, 3> base_color{0, 0, 0};     // added unlit
    std::array<float, 3> lit_color{0.2f, 0.2f, 0.15f};  // the albedo the frame's light falls on
    float wave_length = 1.0f;                     // of the widest wave
    float wave_contrast = 0.5f;                   // 0..1: how much of lit_color a trough loses
    float wave_distortion = 0.01f;                // how far a wave's slope shifts the reflection (picture heights)
    float reflection_strength = 0.5f;
    std::array<float, 3> reflection_tint{1, 1, 1};
    float reflection_blur = 1.0f;                 // in its texels
    float plane_offset = 0.0f;                    // added to the surface's own height
    float glow_strength = 0.3f;
    std::array<float, 3> glow_color{1, 1, 1};
    float glow_size = 1.0f;                       // 1: 48 x 96 picture units
    std::array<WaterQuality, 3> quality{};        // low, medium, high
};
void set_water_surface(WaterSurface surface);
const WaterSurface& water_surface();
// Where the waves are in their cycle, 0..1 (they repeat at 1).
void set_water_phase(float phase);
float water_phase();
// True when a cell of the last world frame has the water surface's material.
bool water_surface_placed();
// What the store holds now, for the modding tools' browsers (read only; the order is the store's).
struct LoadedTexture {
    std::string path;  // gamedata path
    int palette = -1;
    std::shared_ptr<const SceneTexture> texture;  // null: it could not be read
};
std::vector<LoadedTexture> loaded_textures();
struct LoadedModel {
    std::string file;  // the glb ("maps/areas/ER.glb", "convert/models/ITEM/HANDGUN.glb")
    std::shared_ptr<const LibraryModel> model;
};
// Each library model once (not again under its "#<index>" name).
std::vector<LoadedModel> loaded_models();
// The cells read so far, by stem.
std::vector<std::shared_ptr<const CellMesh>> loaded_cells();

// `mods reload`: forgets every cell, model and texture read so far, so the next use reads the files again (a mod's
// replacement that changed or went away); the renderers drop their GPU copies at their next prepare().
void forget_assets();
// Counts forget_assets() calls: what a renderer compares its GPU copies with.
std::uint32_t asset_generation();
// A map is about to be entered: the area files, model files and textures that none of the last kMapsKept maps
// (scene_store.cpp) drew are released, so a session's memory is what a few maps need rather than every map it went
// through. A released file is read again when a later map draws it.
void begin_map();
// Counts the begin_map() calls that released something: the renderers then drop GPU copies of meshes that are no
// longer resident_meshes().
std::uint32_t release_generation();
std::unordered_set<const CellMesh*> resident_meshes();

// Draws frames on one RHI device. GPU copies of cells and textures are made on first use and kept.
class SceneRenderer {
public:
    static Result<std::unique_ptr<SceneRenderer>> create(rhi::Device& device, rhi::TextureFormat color_format);
    ~SceneRenderer();
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    // Uploads what `frame` needs and the device does not have yet. Outside a render pass.
    Result<void> prepare(rhi::CommandList& cmd, const Frame& frame);
    // Draws into the open pass; the caller set the viewport. A pass with depth (D24S8) is expected.
    // item < 0: the map cells; item >= 0: that posed mesh of frame.prelit (each is sorted into the frame on its own,
    // like the PS1 prims it replaces, so effects in front of and behind it keep their order); item >= kObjectItem:
    // frame.objects[item - kObjectItem].
    // layer: Color, or one of the flashlight shadow map's two layers of the same item from view.fs.shadowMatrix:
    // ShadowDepth takes the solid texels, ShadowTint what the blended ones let through (view.fs.shadowQ[3]: the
    // light's far plane).
    enum class Layer { Color, ShadowDepth, ShadowTint };
    Result<void> draw(rhi::RenderPass& pass, const Frame& frame, const View& view, int item, Layer layer = Layer::Color);
    // The water's mirrored picture for the cells of this frame: the posed meshes, mirrored in the plane of the
    // water surface, drawn into the renderer's own small target in a pass of its own (outside any other pass,
    // after prepare() and before the cells are drawn with `view`). Nothing is drawn, and false comes back, when
    // no cell of the frame has the surface.
    bool reflect(rhi::CommandList& cmd, const Frame& frame, const View& view);
    // What the frame's rays can hit (rhi_vulkan_rt.h): the frame's cells and world objects as kept meshes placed in
    // view space, and its posed meshes as one mesh of the frame. After prepare(); isolated models are left out.
    void ray_scene(const Frame& frame, rhi::RayScene* out) const;

    struct Stats {
        std::uint64_t frames = 0;
        std::uint64_t cell_draws = 0;
        std::uint64_t prelit_draws = 0;
        std::uint64_t object_draws = 0;
        std::uint64_t cells_missing = 0;
        std::uint32_t lights = 0;  // the map's lights in the newest frame (Frame::lights)
    };
    const Stats& stats() const { return stats_; }

private:
    struct Impl;
    explicit SceneRenderer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
    Stats stats_;
};

}  // namespace sh::scene
