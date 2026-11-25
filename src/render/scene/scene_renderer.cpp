// SceneRenderer (scene.h): GPU copies of the cells and textures a frame uses, and the draws of one frame.

#include "render/scene/area_cells.h"
#include "render/scene/fog_cutoff.h"
#include "render/scene/materials.h"
#include "render/scene/scene.h"
#include "render/scene/scene_shaders.h"
#include "render/rhi/rhi_vulkan_rt.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include "render/gpu/render_state.h"
#include "render/scene/model_upscale.h"

namespace sh::scene {
namespace {

using namespace sh::rhi;

struct GpuVertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
    std::uint8_t r, g, b, a;
    float box[4];  // CellVertex::box_*; hi < lo = none
};
static_assert(sizeof(GpuVertex) == 52);

GpuVertex gpu_vertex(const CellVertex& s) {
    return {s.x, s.y, s.z, s.nx, s.ny, s.nz, s.u, s.v, 255, 255, 255, 255, {s.box_u0, s.box_v0, s.box_u1, s.box_v1}};
}

constexpr VertexAttribute kAttributes[] = {
    {0, VertexFormat::Float3, 0},
    {1, VertexFormat::Float3, 12},
    {2, VertexFormat::Float2, 24},
    {3, VertexFormat::UByte4Norm, 32},
    {4, VertexFormat::Float4, 36},
};

// std140 SceneVs of scene.vert.
struct VsBlock {
    std::array<float, 16> model_view{};
    std::array<float, 16> ortho{};
    std::array<float, 4> proj{};
    std::array<float, 4> light{};
    std::array<float, 4> tint{};
    std::array<float, 4> far_color{};
    std::array<float, 4> light_vec{};
    std::array<float, 4> light_pos{};
    std::array<float, 4> misc{};
    std::array<float, 16> shadow_matrix{};
    std::array<float, 4> seam{};
};
static_assert(sizeof(VsBlock) == 320);

struct MatBlock {
    std::array<float, 4> mat{};
    std::array<float, 4> rt{};  // ray-traced results bound: x shadow, y reflection, z bounce light
    // x: mip levels of the texture, y and z: the low and high 16 bits of the mask of the map lights that can reach the
    // draw (draw_light_mask; all of them by default)
    std::array<float, 4> mat2{1, 65535, 65535, 0};
};

// std140 SceneWater of scene.frag.
struct WaterBlock {
    std::array<float, 16> world{};      // view space to world
    std::array<float, 4> plane{};       // the surface in view space: unit normal, distance
    std::array<float, 4> proj{};        // ndc = xy * (v.xy / v.z) + zw
    std::array<float, 4> base{};        // rgb: unlit colour, a: wave phase 0..1
    std::array<float, 4> lit{};         // rgb: lit colour, a: wave contrast
    std::array<float, 4> wave{};        // x: radians per view unit, y: reflection shift per slope (uv), zw: blur (uv)
    std::array<float, 4> tint{};        // rgb: reflection tint times strength, a: 1 = there is a mirrored picture
    std::array<float, 4> glow{};        // rgb: glow colour times strength, a: picture height over width
    std::array<float, 4> glow_size{};   // xy: the glow's radii (ndc)
    std::array<float, 4> lamp{};        // xyz: the lamp mirrored in the surface (view space), w: the glow's strength
    std::array<float, 4> level{};       // x: wave layers, y: the wave grid's square (wave radians, 0 none),
                                        // z: colour steps (0 smooth), w: blur taps
};
static_assert(sizeof(WaterBlock) == 224);

// scene.frag's SceneFrame block: the water's, then the lights' (SDL3 GPU has four uniform slots per stage).
struct FrameBlock {
    WaterBlock water;
    LightBlock lights;
};
static_assert(sizeof(FrameBlock) == 224 + 16 * (9 + 3 * kSceneLights + kSceneFlakes + 3 + kFlakeGrid * kFlakeGrid / 2 +
                                               kFlakeList / 4 + kLightTiles / 4));

// scene.frag MapLights: which lights can reach each tile of the picture. A point within a light's range lies inside
// the box around its sphere, so its pixel lies inside the box's projection (x / z is extreme at the box's corners);
// the shader takes its tile from its own view position by the same mapping (ndc = proj.xy * xy / z + proj.zw), and a
// margin covers the rounding of both. A light left out of a tile is one the shader's range test would reject at
// every pixel of it, so the picture is the one of the full loop.
void fill_light_tiles(LightBlock& b, const std::array<float, 4>& proj) {
    for (auto& t : b.tiles) t = {0, 0, 0, 0};
    const int count = std::clamp(static_cast<int>(b.info[0] + 0.5f), 0, static_cast<int>(kSceneLights));
    constexpr float kMargin = 0.05f;  // tiles
    constexpr int kTx = static_cast<int>(kLightTilesX), kTy = static_cast<int>(kLightTilesY);
    for (int i = 0; i < count; ++i) {
        const auto& l = b.pos[static_cast<std::size_t>(i)];
        const float r = l[3];
        const float zn = l[2] - r, zf = l[2] + r;
        if (zf <= 0.0f) continue;  // wholly behind the eye: the shader lights only points in front of it
        int x0 = 0, x1 = kTx - 1, y0 = 0, y1 = kTy - 1;
        if (zn > 1.0f) {
            float lo[2] = {1e30f, 1e30f}, hi[2] = {-1e30f, -1e30f};
            for (int c = 0; c < 8; ++c) {
                const float z = (c & 4) ? zf : zn;
                const float v[2] = {l[0] + ((c & 1) ? r : -r), l[1] + ((c & 2) ? r : -r)};
                for (int a = 0; a < 2; ++a) {
                    const auto ia = static_cast<std::size_t>(a);
                    const float ndc = proj[ia] * (v[a] / z) + proj[ia + 2];
                    lo[a] = std::min(lo[a], ndc);
                    hi[a] = std::max(hi[a], ndc);
                }
            }
            const float t0x = (lo[0] * 0.5f + 0.5f) * kTx - kMargin, t1x = (hi[0] * 0.5f + 0.5f) * kTx + kMargin;
            const float t0y = (lo[1] * 0.5f + 0.5f) * kTy - kMargin, t1y = (hi[1] * 0.5f + 0.5f) * kTy + kMargin;
            if (t1x < 0.0f || t0x >= kTx || t1y < 0.0f || t0y >= kTy) continue;
            x0 = std::clamp(static_cast<int>(std::floor(t0x)), 0, kTx - 1);
            x1 = std::clamp(static_cast<int>(std::floor(t1x)), 0, kTx - 1);
            y0 = std::clamp(static_cast<int>(std::floor(t0y)), 0, kTy - 1);
            y1 = std::clamp(static_cast<int>(std::floor(t1y)), 0, kTy - 1);
        }
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const int t = y * kTx + x;
                b.tiles[static_cast<std::size_t>(t / 4)][static_cast<std::size_t>(t % 4)] |= 1u << i;
            }
    }
}

// The level of the surface's quality table that the settings ask for.
const WaterQuality& water_quality() {
    return water_surface().quality[static_cast<std::size_t>(std::clamp(sh::render::graphics().water_quality, 0, 2))];
}

constexpr std::string_view kVsBlocks[] = {"SceneVs", "WorldFog"};
constexpr std::string_view kFsBlocks[] = {"FsFrame", "WorldFog", "SceneMaterial", "SceneFrame"};
constexpr std::string_view kFsSamplers[] = {"s_tex",       "s_shadow", "s_shadowTint", "s_reflect", "s_rtShadow",
                                            "s_rtReflect", "s_rtGi",   "s_sunShadow",  "s_fogRamp"};
constexpr float kNearPlane = 8.0f;  // view units: geometry nearer than ~3 cm is clipped
// kWorldDepthBias (scene.h) stands in for the PS1 path's world margin (sh::render::precise().world_far_bias), which is
// 0 for the PS1 prims while the scene draws the map. The cells take it along their normal (scene.frag), so along a
// pixel's ray it is never less than about 0.7 of itself in view depth (a plane square to a ray at the screen's corner).
// How much nearer each overlay layer is written than the plane it shares with its ground (CellRange::layer): both
// write their exact depth, so a quarter unit is far above the noise of two interpolations of one plane (a step of
// the 24-bit buffer is 1/64), and the top layer (kMaxOverlayLayer) still stays behind kWorldDepthBias, so a prop or a
// mark on an overlay keeps winning against it.
constexpr float kOverlayLift = 0.25f;
static_assert(kOverlayLift * kMaxOverlayLayer < kWorldDepthBias * 0.7f);
// Pipeline kinds: posed meshes are drawn two-sided, cells and objects one-sided, their overlays one-sided; every
// kind writes the fragment's own depth.
constexpr int kNoCull = 0;
constexpr int kCullBack = 1;
constexpr int kOverlay = 2;
constexpr int kPipelineKinds = 3;
constexpr int kCellUnits = 40 * 256;

// An axis-aligned box in a mesh's own space; lo > hi: empty.
struct Bounds {
    std::array<float, 3> lo{1e30f, 1e30f, 1e30f};
    std::array<float, 3> hi{-1e30f, -1e30f, -1e30f};
    void add(float x, float y, float z) {
        lo = {std::min(lo[0], x), std::min(lo[1], y), std::min(lo[2], z)};
        hi = {std::max(hi[0], x), std::max(hi[1], y), std::max(hi[2], z)};
    }
    bool empty() const { return lo[0] > hi[0]; }
};

// The map lights (bit i: light i of `b`) whose range reaches the box `box` placed by `mv` (mesh space to view space,
// column-major): a point the light reaches lies in the box, so the box's view-space hull lies within the range of
// the light's centre. The shader tries only these at the draw's pixels (scene.frag MapLights), with the picture of
// trying all of them.
std::uint32_t draw_light_mask(const LightBlock& b, const Bounds& box, const std::array<float, 16>& mv) {
    const int count = std::clamp(static_cast<int>(b.info[0] + 0.5f), 0, static_cast<int>(kSceneLights));
    if (count == 0 || box.empty()) return count == 0 ? 0u : ~0u;
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (int k = 0; k < 8; ++k) {
        const float p[3] = {(k & 1) ? box.hi[0] : box.lo[0], (k & 2) ? box.hi[1] : box.lo[1],
                            (k & 4) ? box.hi[2] : box.lo[2]};
        for (std::size_t r = 0; r < 3; ++r) {
            const float v = mv[r] * p[0] + mv[4 + r] * p[1] + mv[8 + r] * p[2] + mv[12 + r];
            lo[r] = std::min(lo[r], v);
            hi[r] = std::max(hi[r], v);
        }
    }
    std::uint32_t mask = 0;
    for (int i = 0; i < count; ++i) {
        const auto& l = b.pos[static_cast<std::size_t>(i)];
        float d2 = 0.0f;
        for (std::size_t r = 0; r < 3; ++r) {
            const float e = std::max({lo[r] - l[r], 0.0f, l[r] - hi[r]});
            d2 += e * e;
        }
        // A margin for the rounding of the box's corners.
        const float reach = l[3] * 1.001f + 1.0f;
        if (d2 <= reach * reach) mask |= 1u << i;
    }
    return mask;
}

// What a draw leaves out (RENDER.md 7.7), tested on the box `b` placed by `mv` (mesh space to view space,
// column-major). In colour: what lies wholly past the fog's draw cutoff (view depth), behind the near plane or beside
// the picture; the sides come from the scene's projection (scene.vert SceneProject: clip x = ortho x * (H * x +
// centre x * z) + ortho w * z, w = z) and are widened a little for the seam lines' pixel shifts and TAA's jitter. In a
// shadow layer: what lies wholly outside one side or the far plane of the light's clip space (GL convention); the
// light's near plane is never used, what lies before it still casts where the depth is clamped there.
struct DrawCull {
    float cut_z = kNoFogCutoff;
    float near_z = 0.0f;
    std::array<float, 4> sides{};  // x: a, b; y: a, b of the clip rows a * p + b * z
    const std::array<float, 16>* light = nullptr;

    bool out(const Bounds& b, const std::array<float, 16>& mv) const {
        if (b.empty()) return false;
        constexpr float kWiden = 1.02f;
        unsigned all_out = 0x7F;  // x > w, x < -w, y > w, y < -w, past the far plane, before the near plane
        for (int k = 0; k < 8 && all_out; ++k) {
            const float p[3] = {(k & 1) ? b.hi[0] : b.lo[0], (k & 2) ? b.hi[1] : b.lo[1], (k & 4) ? b.hi[2] : b.lo[2]};
            float v[3];
            for (std::size_t r = 0; r < 3; ++r) v[r] = mv[r] * p[0] + mv[4 + r] * p[1] + mv[8 + r] * p[2] + mv[12 + r];
            float x, y, z, w;
            unsigned bits = 0;
            if (light) {
                const auto& l = *light;
                x = l[0] * v[0] + l[4] * v[1] + l[8] * v[2] + l[12];
                y = l[1] * v[0] + l[5] * v[1] + l[9] * v[2] + l[13];
                z = l[2] * v[0] + l[6] * v[1] + l[10] * v[2] + l[14];
                w = l[3] * v[0] + l[7] * v[1] + l[11] * v[2] + l[15];
                if (z > w) bits |= 32;
            } else {
                x = sides[0] * v[0] + sides[1] * v[2];
                y = sides[2] * v[1] + sides[3] * v[2];
                w = v[2] * kWiden;
                if (v[2] > cut_z) bits |= 32;
                if (v[2] < near_z) bits |= 64;
            }
            if (x > w) bits |= 1;
            if (x < -w) bits |= 2;
            if (y > w) bits |= 4;
            if (y < -w) bits |= 8;
            // Behind the eye the sides flip: only the near plane may leave such a corner out.
            if (!light && v[2] <= 0.0f) bits &= 64;
            all_out &= bits;
        }
        return all_out != 0;
    }
};

struct GpuCell {
    std::shared_ptr<const CellMesh> mesh;
    BufferH vertices, indices;
    std::vector<TextureH> textures;  // per material; empty handle = white
    bool ok = false;
    bool textures_pending = false;   // a material's upload failed: retried by later frames
    // Per material without a picture (CellMaterial::unpictured): the mean height of its vertices (view units), the
    // plane of a water surface. Empty when the mesh has no such material.
    std::vector<std::optional<float>> surface_y;
    // Model upscaling of a world object (model_upscale.h): patch normals, made on first use; the mean edge and the
    // largest distance from the origin set its level.
    BufferH patch_normals;
    bool patch_tried = false;
    double mean_edge = 0, radius = 0;
    // Model upscaling of a map cell (pn_surface), made on first use: the cell's vertices followed by a copy of every
    // corner of its curved triangles (their patch data in patch_normals), and per range its flat triangles, then its
    // curved ones. No buffers when nothing of the cell bends.
    struct UpRange {
        std::uint32_t first = 0, flat = 0, curved = 0;  // first index; triangles
    };
    BufferH up_vertices, up_indices;
    std::vector<UpRange> up_ranges;
    std::uint32_t curved_triangles = 0;        // mean_edge is theirs
    std::array<double, 3> box_lo{}, box_hi{};  // of the cell's vertices
    // The mesh's box and each range's (in mesh space): what the fog or a light leaves out. A range is also cut into
    // runs of consecutive triangles (kChunkTriangles), each with its box: a cell spans 40 m, and one material of it
    // reaches from under Harry's feet into the fog. The runs keep the triangles' order; neighbours drawn are one draw.
    struct Chunk {
        std::uint32_t first = 0, count = 0;  // indices
        Bounds bounds;
    };
    Bounds bounds;
    std::vector<Bounds> range_bounds;
    std::vector<std::vector<Chunk>> range_chunks;
};

constexpr std::uint32_t kChunkTriangles = 64;

// A texture whose picture exists but whose device upload failed. Such a failure is the device's state at that moment
// (memory, an error another renderer on the shared context left pending), not the picture's, so it is tried again
// instead of drawing the material untextured for the rest of the session.
struct UploadRetry {
    std::uint64_t at = 0;  // first frame of the next attempt
    int tries = 0;
};
constexpr std::uint64_t kRetryMaxWait = 120;  // frames between attempts, at most

struct PrelitDraw {
    std::uint32_t first = 0, count = 0;
    TextureH texture;
    Blend blend = Blend::Opaque;
    bool split_stp = false;
    bool all_stp = false;
    bool casts_shadow = true;
    bool isolated = false;
    bool depth_reset = false;
    float h = 0, geom_ofs_x = 0, geom_ofs_y = 0;
    int level = 0;  // model upscaling: 0 = the mesh has no patch normals, 1 = drawn plain this frame
    Bounds bounds;  // of its vertices, view space
};

// Upscaled pipeline slots (ModelUpscaler::pipeline): kind * kBlendCount + blend, then the two shadow layers.
constexpr int kUpscaleCasterSlot = kPipelineKinds * kBlendCount;
constexpr int kUpscaleCasterTintSlot = kUpscaleCasterSlot + 1;
constexpr int kUpscaleMapSlots = kUpscaleCasterTintSlot + 1;  // added for the map's twins, then for wireframe ones
constexpr int kUpscaleWireSlots = 2 * kUpscaleMapSlots;
constexpr int kUpscaleCasterLevel = 4;  // the most level of a map surface in the shadow maps

}  // namespace

struct SceneRenderer::Impl {
    Device& device;
    TextureFormat color_format;
    ShaderH vs, fs;
    ShaderH fs_exact;  // scene.frag writing the fragment's own depth
    ShaderH fs_quad;   // fs_exact sharing shadows and map lights over 2x2 quads (SH_SCENE_QUAD), or none
    ShaderH fs_quad_raster;  // fs_quad without the fragment's own depth (late seams)
    // The light tiles last made (fill_light_tiles) and what they were made from.
    std::array<std::array<std::uint32_t, 4>, kLightTiles / 4> light_tiles{};
    std::array<float, 4> light_tiles_proj{};
    std::array<std::array<float, 4>, kSceneLights> light_tiles_pos{};
    float light_tiles_count = -1.0f;
    // [kind][Blend]
    PipelineH pipelines[kPipelineKinds][kBlendCount];
    // The same, drawn as edges (ViewerOverrides::wireframe).
    PipelineH wire_pipelines[kPipelineKinds][kBlendCount];
    PipelineH pick(int kind, int blend, bool wire) const {
        return wire && wire_pipelines[kind][blend] ? wire_pipelines[kind][blend] : pipelines[kind][blend];
    }
    // The descriptions of the pipelines above, for their upscaled twins (model_upscale.h).
    PipelineDesc descs[kPipelineKinds][kBlendCount];
    PipelineDesc caster_desc, caster_tint_desc;
    std::unique_ptr<ModelUpscaler> upscale;
    BufferH prelit_patch_normals;
    std::uint32_t prelit_patch_cap = 0;
    std::vector<std::array<float, 4>> scratch_patch_normals;
    std::vector<int> object_levels;  // per frame.objects entry; 0 = plain
    std::vector<int> cell_levels;    // per frame.cells entry: the most level of its curved surfaces; below 2 = plain
    float px_per_unit = 0;           // window pixels per screen unit at the last world view
    PipelineH caster;  // depth only, both faces: the flashlight shadow map
    // The shadow map's colour layer: rgb multiplied by what the surface lets through, alpha the nearest distance.
    PipelineH caster_tint;
    PipelineH seam;    // opaque line list: CellMesh::seams
    PipelineH seam_late;  // the same with the rasterised depth, drawn after the opaque surfaces (quad shading)
    PipelineH depth_reset;  // PrelitMesh::depth_reset: depth always written, no colour
    SamplerH linear, nearest;
    SamplerH linear_clamp, nearest_clamp;  // the water's mirrored picture
    TextureH white;
    std::map<std::string, GpuCell> cells;
    std::map<const CellMesh*, GpuCell> models;  // library models of ObjectRef (the store keeps each mesh for good)
    std::map<std::pair<std::string, int>, TextureH> textures;
    std::map<decltype(TextureH::raw), std::uint32_t> mip_levels;  // the textures with a mip chain, by handle
    std::map<std::uint64_t, TextureH> prelit_textures;
    // The water's mirrored picture (SceneRenderer::reflect) and the frame it was drawn for.
    TextureH reflect_color, reflect_depth;
    int reflect_w = 0, reflect_h = 0;
    std::uint64_t reflect_serial = 0;
    bool reflect_valid = false;
    TextureH black;
    TextureH highlight;  // ViewerOverrides::highlight_texture
    // Set while the mirrored picture is drawn: the posed meshes' view-space mirror in the water's plane.
    std::optional<std::array<float, 16>> mirror;
    std::map<std::pair<std::string, int>, UploadRetry> texture_retry;
    std::map<std::uint64_t, UploadRetry> prelit_retry;
    std::uint64_t frame_no = 0;
    std::uint32_t asset_generation = 0;  // sh::scene::asset_generation() the copies below were made in
    std::uint32_t release_generation = 0;  // sh::scene::release_generation() last pruned against
    BufferH prelit_vertices, prelit_indices;
    std::uint32_t prelit_vertex_cap = 0, prelit_index_cap = 0;
    std::vector<PrelitDraw> prelit_draws;
    std::vector<GpuVertex> scratch_vertices;
    std::vector<std::uint32_t> scratch_indices;

    explicit Impl(Device& d, TextureFormat f) : device(d), color_format(f) {}

    // Model upscaling: the patch normals of the posed meshes and the level of every model this frame. A model is a
    // character (its meshes share PrelitMesh::upscale_model) or a world object.
    void upscale_levels(CommandList& cmd, const Frame& frame) {
        std::vector<PnModel> models;
        std::vector<std::uint32_t> prelit_model(prelit_draws.size(), 0);
        std::map<std::uint32_t, std::size_t> model_of;
        for (std::size_t i = 0; i < prelit_draws.size(); ++i) {
            const PrelitDraw& d = prelit_draws[i];
            if (d.level == 0) continue;
            const std::uint32_t key = frame.prelit[i].upscale_model;
            auto [it, added] =
                model_of.try_emplace(key ? key : 0x80000000u + static_cast<std::uint32_t>(i), models.size());
            if (added) models.push_back({0.0, 1e30, 0});
            PnModel& m = models[it->second];
            prelit_model[i] = static_cast<std::uint32_t>(it->second) + 1;
            for (std::uint32_t t = 0; t + 2 < d.count; t += 3) {
                const std::uint32_t* ix = &scratch_indices[d.first + t];
                for (int k = 0; k < 3; ++k) {
                    const GpuVertex& a = scratch_vertices[ix[k]];
                    const GpuVertex& b = scratch_vertices[ix[(k + 1) % 3]];
                    m.mean_edge += std::sqrt(double(a.x - b.x) * (a.x - b.x) + double(a.y - b.y) * (a.y - b.y) +
                                             double(a.z - b.z) * (a.z - b.z));
                    if (a.z > 0) m.nearest_z = std::min(m.nearest_z, double(a.z));
                }
                ++m.triangles;
            }
        }
        for (PnModel& m : models) m.mean_edge = m.triangles ? m.mean_edge / (3.0 * m.triangles) : 0.0;
        std::vector<std::size_t> object_model(frame.objects.size(), SIZE_MAX);
        const Camera& cam = frame.camera;
        for (std::size_t i = 0; i < frame.objects.size(); ++i) {
            const ObjectRef& o = frame.objects[i];
            auto it = o.mesh ? models_find(o.mesh.get()) : nullptr;
            if (!it || !it->patch_normals || it->mean_edge <= 0) continue;
            const double scale = std::sqrt(o.rot[0] * o.rot[0] + o.rot[3] * o.rot[3] + o.rot[6] * o.rot[6]);
            const double z = cam.rot[6] * o.trans[0] + cam.rot[7] * o.trans[1] + cam.rot[8] * o.trans[2] + cam.trans[2];
            std::uint32_t triangles = 0;
            for (const CellRange& r : it->mesh->ranges) triangles += r.count / 3;
            object_model[i] = models.size();
            models.push_back({it->mean_edge * scale, std::max(z - it->radius * scale, 16.0), triangles});
        }
        const std::vector<int> levels = pn_levels(models, frame.h, px_per_unit);
        for (std::size_t i = 0; i < prelit_draws.size(); ++i)
            if (prelit_model[i]) prelit_draws[i].level = levels[prelit_model[i] - 1];
        for (std::size_t i = 0; i < frame.objects.size(); ++i)
            if (object_model[i] != SIZE_MAX) object_levels[i] = levels[object_model[i]];
        if (scratch_patch_normals.empty()) return;
        scratch_patch_normals.resize(scratch_vertices.size(), {0.0f, 0.0f, 1.0f, 1.0f});
        const auto bytes = static_cast<std::uint32_t>(scratch_patch_normals.size() * sizeof(scratch_patch_normals[0]));
        if (auto r = ensure_buffer(prelit_patch_normals, prelit_patch_cap, bytes, BufferUsage::Vertex); !r) {
            for (PrelitDraw& d : prelit_draws) d.level = 0;
            return;
        }
        cmd.uploadBuffer(prelit_patch_normals, 0, std::as_bytes(std::span(scratch_patch_normals)));
    }

    const GpuCell* models_find(const CellMesh* mesh) const {
        auto it = models.find(mesh);
        return it != models.end() && it->second.ok ? &it->second : nullptr;
    }

    void release(GpuCell& c) {
        for (BufferH b : {c.vertices, c.indices, c.patch_normals, c.up_vertices, c.up_indices})
            if (b) device.destroy(b);
    }

    // The store forgot its files (`mods reload`): the GPU copies of them go too and are made again on next use.
    void drop_assets() {
        for (auto& [k, c] : cells) release(c);
        for (auto& [k, c] : models) release(c);
        for (auto& [k, t] : textures)
            if (t) device.destroy(t);
        for (auto& [k, t] : prelit_textures)
            if (t) device.destroy(t);
        cells.clear();
        models.clear();
        textures.clear();
        mip_levels.clear();
        prelit_textures.clear();
        texture_retry.clear();
        prelit_retry.clear();
    }

    // Better pictures of loaded textures (refresh_textures): their copies go and every mesh drawing with one asks for
    // it again, a cell or model in view within this frame, so the old picture is never replaced by white.
    void drop_refreshed(const RefreshedTextures& r) {
        std::set<std::uint64_t> gone;
        for (const auto& key : r.textures) {
            texture_retry.erase(key);
            auto it = textures.find(key);
            if (it == textures.end()) continue;
            if (it->second) {
                gone.insert(it->second.raw);
                mip_levels.erase(it->second.raw);
                device.destroy(it->second);
            }
            textures.erase(it);
        }
        auto unbind = [&gone](auto& gpu) {
            for (auto& [k, c] : gpu) {
                for (TextureH& t : c.textures) {
                    if (!t || !gone.count(t.raw)) continue;
                    t = {};
                    c.textures_pending = true;
                }
            }
        };
        if (!gone.empty()) {
            unbind(cells);
            unbind(models);
        }
        for (const std::uint64_t key : r.prelit) {
            prelit_retry.erase(key);
            auto it = prelit_textures.find(key);
            if (it == prelit_textures.end()) continue;
            if (it->second) {
                mip_levels.erase(it->second.raw);
                device.destroy(it->second);
            }
            prelit_textures.erase(it);
        }
    }

    // The store released the files of maps left a while ago (begin_map): the GPU copies of their meshes go, and
    // every texture that no remaining mesh draws with.
    void drop_released() {
        const std::unordered_set<const CellMesh*> live = resident_meshes();
        auto prune = [&](auto& gpu) {
            for (auto it = gpu.begin(); it != gpu.end();) {
                GpuCell& c = it->second;
                if (!c.mesh || live.count(c.mesh.get())) {
                    ++it;
                    continue;
                }
                release(c);
                it = gpu.erase(it);
            }
        };
        prune(cells);
        prune(models);
        std::set<std::uint64_t> drawn;
        auto note = [&drawn](const auto& gpu) {
            for (const auto& [k, c] : gpu)
                for (TextureH t : c.textures)
                    if (t) drawn.insert(t.raw);
        };
        note(cells);
        note(models);
        for (auto it = textures.begin(); it != textures.end();) {
            if (!it->second || drawn.count(it->second.raw)) {
                ++it;
                continue;
            }
            mip_levels.erase(it->second.raw);
            device.destroy(it->second);
            texture_retry.erase(it->first);
            it = textures.erase(it);
        }
    }

    ~Impl() {
        for (auto& [k, c] : cells) release(c);
        for (auto& [k, c] : models) release(c);
        if (prelit_patch_normals) device.destroy(prelit_patch_normals);
        for (auto& [k, t] : textures)
            if (t) device.destroy(t);
        for (auto& [k, t] : prelit_textures)
            if (t) device.destroy(t);
        if (reflect_color) device.destroy(reflect_color);
        if (reflect_depth) device.destroy(reflect_depth);
        if (black) device.destroy(black);
        if (highlight) device.destroy(highlight);
        if (prelit_vertices) device.destroy(prelit_vertices);
        if (prelit_indices) device.destroy(prelit_indices);
        if (white) device.destroy(white);
        for (auto& row : pipelines)
            for (PipelineH p : row)
                if (p) device.destroy(p);
        for (auto& row : wire_pipelines)
            for (PipelineH p : row)
                if (p) device.destroy(p);
        if (caster) device.destroy(caster);
        if (caster_tint) device.destroy(caster_tint);
        if (seam) device.destroy(seam);
        if (seam_late) device.destroy(seam_late);
        if (depth_reset) device.destroy(depth_reset);
        if (linear_clamp) device.destroy(linear_clamp);
        if (nearest_clamp) device.destroy(nearest_clamp);
        if (linear) device.destroy(linear);
        if (nearest) device.destroy(nearest);
        if (vs) device.destroy(vs);
        if (fs) device.destroy(fs);
        if (fs_exact) device.destroy(fs_exact);
        if (fs_quad) device.destroy(fs_quad);
        if (fs_quad_raster) device.destroy(fs_quad_raster);
    }

    // Every texture is its own device texture at its own size (any size, power of two or not). Above what the device
    // takes it is scaled down rather than dropped: a replacement picture too big for the GPU still draws.
    Result<TextureH> upload_texture(CommandList& cmd, const SceneTexture& source, std::string_view name) {
        if (source.width <= 0 || source.height <= 0 ||
            source.rgba.size() < static_cast<std::size_t>(source.width) * source.height * 4)
            return Result<TextureH>::fail(std::string(name) + ": no pixels");
        const int limit = static_cast<int>(std::min<std::uint32_t>(device.caps().maxTextureSize, 1u << 30));
        std::optional<SceneTexture> scaled;
        if (limit > 0 && (source.width > limit || source.height > limit)) scaled = fit_texture(source, limit);
        for (;;) {
            const SceneTexture& t = scaled ? *scaled : source;
            // An upscaled picture is minified at a distance: its levels are built here, the shader picks them.
            const std::vector<SceneTexture> made =
                t.mipmapped && (scaled || t.levels.empty()) ? sh::scene::mip_levels(t) : std::vector<SceneTexture>{};
            const std::vector<SceneTexture>& mips = t.mipmapped && !scaled && !t.levels.empty() ? t.levels : made;
            TextureDesc desc{TextureFormat::RGBA8, static_cast<std::uint32_t>(t.width),
                             static_cast<std::uint32_t>(t.height), kUsageSampled, name};
            desc.mipLevels = static_cast<std::uint32_t>(mips.size() + 1);
            auto tex = device.createTexture(desc);
            if (tex) {
                if (scaled)
                    log(1, "[SCENE] " + std::string(name) + " " + std::to_string(source.width) + "x" +
                               std::to_string(source.height) + " drawn at " + std::to_string(t.width) + "x" +
                               std::to_string(t.height) + " (the device limit)");
                cmd.uploadTexture(tex.value(), {0, 0, t.width, t.height}, std::as_bytes(std::span(t.rgba)));
                if (!mips.empty()) mip_levels[tex.value().raw] = static_cast<std::uint32_t>(mips.size() + 1);
                for (std::size_t l = 0; l < mips.size(); ++l)
                    cmd.uploadTextureLevel(tex.value(), static_cast<std::uint32_t>(l + 1),
                                           {0, 0, mips[l].width, mips[l].height},
                                           std::as_bytes(std::span(mips[l].rgba)));
                return tex;
            }
            // A backend that reports no limit: halve until it fits. Where the limit is known and the size is within
            // it, the refusal is not about the size; the caller tries the full size again later.
            if (limit > 0 || (t.width <= 1 && t.height <= 1)) return tex;
            scaled = fit_texture(t, std::max(1, std::max(t.width, t.height) / 2));
        }
    }

    // False while a failed upload waits for its next attempt.
    template <typename Key>
    bool retry_due(const std::map<Key, UploadRetry>& retry, const Key& key) const {
        auto it = retry.find(key);
        return it == retry.end() || frame_no >= it->second.at;
    }
    template <typename Key>
    void upload_failed(std::map<Key, UploadRetry>& retry, const Key& key, const std::string& what,
                       const std::string& error) {
        UploadRetry& r = retry[key];
        if (r.tries == 0) log(1, "[SCENE] " + what + ": upload failed (" + error + "); tried again on later frames");
        ++r.tries;
        r.at = frame_no + std::min<std::uint64_t>(kRetryMaxWait, std::uint64_t{1} << std::min(r.tries - 1, 7));
    }

    // Empty for no texture: no path, a picture that cannot be read (for good), or an upload that failed and waits
    // for its next attempt (`waits` set; the cell asks again on later frames).
    TextureH texture_for(CommandList& cmd, const CellMaterial& m, bool& waits) {
        if (m.texture.empty()) return {};
        const auto key = std::make_pair(m.texture, m.palette);
        if (auto it = textures.find(key); it != textures.end()) return it->second;
        const std::shared_ptr<const SceneTexture> t = find_texture(m.texture, m.palette);
        if (!t) {
            textures.emplace(key, TextureH{});
            return {};
        }
        waits = true;
        if (!retry_due(texture_retry, key)) return {};
        auto up = upload_texture(cmd, *t, "scene texture");
        if (!up) {
            upload_failed(texture_retry, key, m.texture, up.error());
            return {};
        }
        waits = false;
        texture_retry.erase(key);
        textures.emplace(key, up.value());
        return up.value();
    }

    void resolve_textures(CommandList& cmd, GpuCell& g) {
        const CellMesh& m = *g.mesh;
        g.textures.resize(m.materials.size());
        bool pending = false;
        for (std::size_t i = 0; i < m.materials.size(); ++i) {
            if (g.textures[i]) continue;
            bool waits = false;
            g.textures[i] = texture_for(cmd, m.materials[i], waits);
            pending = pending || waits;
        }
        g.textures_pending = pending;
    }

    Result<void> make_cell(CommandList& cmd, GpuCell& g) {
        const CellMesh& m = *g.mesh;
        if (m.vertices.empty() || m.indices.empty()) return Result<void>::ok();
        scratch_vertices.resize(m.vertices.size());
        for (std::size_t i = 0; i < m.vertices.size(); ++i) scratch_vertices[i] = gpu_vertex(m.vertices[i]);
        const bool shader_read = device.caps().storageBuffers;
        auto vb = device.createBuffer({BufferUsage::Vertex,
                                       static_cast<std::uint32_t>(scratch_vertices.size() * sizeof(GpuVertex)),
                                       "scene cell", shader_read});
        if (!vb) return Result<void>::fail(vb.error());
        auto ib = device.createBuffer(
            {BufferUsage::Index, static_cast<std::uint32_t>(m.indices.size() * 4), "scene cell", shader_read});
        if (!ib) {
            device.destroy(vb.value());
            return Result<void>::fail(ib.error());
        }
        g.vertices = vb.value();
        g.indices = ib.value();
        cmd.uploadBuffer(g.vertices, 0, std::as_bytes(std::span(scratch_vertices)));
        cmd.uploadBuffer(g.indices, 0, std::as_bytes(std::span(m.indices)));
        g.textures.clear();
        resolve_textures(cmd, g);
        g.bounds = {};
        for (const CellVertex& v : m.vertices) g.bounds.add(v.x, v.y, v.z);
        g.range_bounds.assign(m.ranges.size(), {});
        g.range_chunks.assign(m.ranges.size(), {});
        for (std::size_t ri = 0; ri < m.ranges.size(); ++ri) {
            const CellRange& r = m.ranges[ri];
            const std::uint32_t end = std::min<std::uint32_t>(r.first + r.count, static_cast<std::uint32_t>(m.indices.size()));
            for (std::uint32_t c = r.first; c < end; c += kChunkTriangles * 3) {
                GpuCell::Chunk chunk;
                chunk.first = c;
                chunk.count = std::min(kChunkTriangles * 3, end - c);
                for (std::uint32_t i = c; i < c + chunk.count; ++i) {
                    const CellVertex& v = m.vertices[m.indices[i]];
                    chunk.bounds.add(v.x, v.y, v.z);
                    g.range_bounds[ri].add(v.x, v.y, v.z);
                }
                g.range_chunks[ri].push_back(chunk);
            }
        }
        g.surface_y.clear();
        for (const CellRange& r : m.ranges) {
            const auto mat = static_cast<std::size_t>(r.material);
            if (mat >= m.materials.size() || m.materials[mat].unpictured.empty() || r.count == 0) continue;
            g.surface_y.resize(m.materials.size());
            if (g.surface_y[mat]) continue;
            double sum = 0;
            for (std::uint32_t i = 0; i < r.count; ++i) sum += m.vertices[m.indices[r.first + i]].y;
            g.surface_y[mat] = static_cast<float>(sum / r.count);
        }
        g.ok = true;
        return Result<void>::ok();
    }

    // A world object's patch normals: its vertices welded by position, the file's normals deciding the creases.
    void make_patch_normals(CommandList& cmd, GpuCell& g) {
        g.patch_tried = true;
        const CellMesh& m = *g.mesh;
        if (m.from_mod) return;
        std::vector<std::string> keys(m.vertices.size());
        std::vector<std::array<float, 3>> normals(m.vertices.size());
        for (std::size_t i = 0; i < m.vertices.size(); ++i) {
            const CellVertex& v = m.vertices[i];
            keys[i].assign(reinterpret_cast<const char*>(&v.x), 3 * sizeof(float));
            normals[i] = {v.nx, v.ny, v.nz};
            g.radius = std::max(g.radius, std::sqrt(double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z));
        }
        std::vector<std::array<float, 4>> pn;
        std::vector<float> xyz(m.vertices.size() * 3);
        for (std::size_t i = 0; i < m.vertices.size(); ++i) {
            xyz[i * 3] = m.vertices[i].x;
            xyz[i * 3 + 1] = m.vertices[i].y;
            xyz[i * 3 + 2] = m.vertices[i].z;
        }
        std::vector<std::uint32_t> tri_list;
        for (const CellRange& r : m.ranges)
            tri_list.insert(tri_list.end(), m.indices.begin() + r.first, m.indices.begin() + r.first + r.count);
        pn_normals(pn_weld(keys, normals), normals, xyz, tri_list, pn, true);
        double edges = 0;
        std::size_t triangles = 0;
        for (const CellRange& r : m.ranges) {
            for (std::uint32_t t = 0; t + 2 < r.count; t += 3) {
                const std::uint32_t* ix = &m.indices[r.first + t];
                for (int k = 0; k < 3; ++k) {
                    const CellVertex& a = m.vertices[ix[k]];
                    const CellVertex& b = m.vertices[ix[(k + 1) % 3]];
                    edges += std::sqrt(double(a.x - b.x) * (a.x - b.x) + double(a.y - b.y) * (a.y - b.y) +
                                       double(a.z - b.z) * (a.z - b.z));
                }
                ++triangles;
            }
        }
        g.mean_edge = triangles ? edges / (3.0 * triangles) : 0.0;
        auto b = device.createBuffer(
            {BufferUsage::Vertex, static_cast<std::uint32_t>(pn.size() * sizeof(pn[0])), "scene patch normals", true});
        if (!b) {
            log(1, "[SCENE] patch normals of " + m.stem + ": " + b.error());
            return;
        }
        g.patch_normals = b.value();
        cmd.uploadBuffer(g.patch_normals, 0, std::as_bytes(std::span(pn)));
    }

    // A map cell's curved surfaces (pn_surface): the water and the overlays stay flat, and so does what lies under an
    // overlay.
    void make_cell_patches(CommandList& cmd, GpuCell& g) {
        g.patch_tried = true;
        const CellMesh& m = *g.mesh;
        if (m.from_mod) return;
        std::vector<float> xyz(m.vertices.size() * 3);
        for (std::size_t i = 0; i < m.vertices.size(); ++i) {
            xyz[i * 3] = m.vertices[i].x;
            xyz[i * 3 + 1] = m.vertices[i].y;
            xyz[i * 3 + 2] = m.vertices[i].z;
        }
        std::vector<std::uint32_t> tri_list;
        std::vector<std::uint8_t> flags;
        for (const CellRange& r : m.ranges) {
            const auto mat = static_cast<std::size_t>(r.material);
            const bool water = mat < m.materials.size() && !m.materials[mat].unpictured.empty();
            const std::uint8_t f = r.layer > 0 ? kPnOverlay : (water ? kPnPinned : 0);
            tri_list.insert(tri_list.end(), m.indices.begin() + r.first, m.indices.begin() + r.first + r.count / 3 * 3);
            flags.insert(flags.end(), r.count / 3, f);
        }
        const PnSurface surface = pn_surface(xyz, tri_list, flags);
        if (surface.curved_count == 0) return;

        std::vector<GpuVertex> verts;
        verts.reserve(m.vertices.size() + surface.curved_count * 3);
        for (const CellVertex& v : m.vertices) verts.push_back(gpu_vertex(v));
        // Two per vertex (PnSurface::corners); the cell's own vertices are never drawn as patches.
        std::vector<std::array<float, 4>> pn(m.vertices.size() * 2, {0.0f, 0.0f, 0.0f, 2.0f});
        std::vector<std::uint32_t> idx;
        idx.reserve(tri_list.size());
        g.up_ranges.assign(m.ranges.size(), {});
        std::size_t t0 = 0;
        for (std::size_t ri = 0; ri < m.ranges.size(); ++ri) {
            const std::size_t n = m.ranges[ri].count / 3;
            GpuCell::UpRange& u = g.up_ranges[ri];
            u.first = static_cast<std::uint32_t>(idx.size());
            for (std::size_t t = t0; t < t0 + n; ++t) {
                if (surface.curved[t]) continue;
                idx.insert(idx.end(), tri_list.begin() + t * 3, tri_list.begin() + t * 3 + 3);
                ++u.flat;
            }
            for (std::size_t t = t0; t < t0 + n; ++t) {
                if (!surface.curved[t]) continue;
                for (std::size_t k = 0; k < 3; ++k) {
                    idx.push_back(static_cast<std::uint32_t>(verts.size()));
                    verts.push_back(gpu_vertex(m.vertices[tri_list[t * 3 + k]]));
                    pn.push_back(surface.corners[(t * 3 + k) * 2]);
                    pn.push_back(surface.corners[(t * 3 + k) * 2 + 1]);
                }
                ++u.curved;
            }
            t0 += n;
        }
        const bool shader_read = device.caps().storageBuffers;
        auto vb =
            device.createBuffer({BufferUsage::Vertex, static_cast<std::uint32_t>(verts.size() * sizeof(GpuVertex)),
                                 "scene cell upscaled", shader_read});
        auto ib = device.createBuffer(
            {BufferUsage::Index, static_cast<std::uint32_t>(idx.size() * 4), "scene cell upscaled", shader_read});
        auto pb = device.createBuffer(
            {BufferUsage::Vertex, static_cast<std::uint32_t>(pn.size() * sizeof(pn[0])), "scene patch normals", true});
        if (!vb || !ib || !pb) {
            log(1, "[SCENE] patch normals of " + m.stem + ": " + (!vb ? vb.error() : !ib ? ib.error() : pb.error()));
            if (vb) device.destroy(vb.value());
            if (ib) device.destroy(ib.value());
            if (pb) device.destroy(pb.value());
            g.up_ranges.clear();
            return;
        }
        g.up_vertices = vb.value();
        g.up_indices = ib.value();
        g.patch_normals = pb.value();
        g.curved_triangles = surface.curved_count;
        double edges = 0;
        for (std::size_t t = 0; t < surface.curved.size(); ++t) {
            if (!surface.curved[t]) continue;
            for (std::size_t k = 0; k < 3; ++k) {
                const std::size_t a = tri_list[t * 3 + k] * 3, b = tri_list[t * 3 + (k + 1) % 3] * 3;
                edges += std::sqrt(double(xyz[a] - xyz[b]) * (xyz[a] - xyz[b]) +
                                   double(xyz[a + 1] - xyz[b + 1]) * (xyz[a + 1] - xyz[b + 1]) +
                                   double(xyz[a + 2] - xyz[b + 2]) * (xyz[a + 2] - xyz[b + 2]));
            }
        }
        g.mean_edge = edges / (3.0 * surface.curved_count);
        g.box_lo = {1e30, 1e30, 1e30};
        g.box_hi = {-1e30, -1e30, -1e30};
        for (std::size_t i = 0; i + 2 < xyz.size(); i += 3)
            for (std::size_t c = 0; c < 3; ++c) {
                g.box_lo[c] = std::min(g.box_lo[c], double(xyz[i + c]));
                g.box_hi[c] = std::max(g.box_hi[c], double(xyz[i + c]));
            }
        cmd.uploadBuffer(g.up_vertices, 0, std::as_bytes(std::span(verts)));
        cmd.uploadBuffer(g.up_indices, 0, std::as_bytes(std::span(idx)));
        cmd.uploadBuffer(g.patch_normals, 0, std::as_bytes(std::span(pn)));
    }

    // The height (world units) of the water surface among the frame's cells; none when no cell has its material.
    std::optional<float> water_plane(const Frame& frame) const {
        const WaterSurface& w = water_surface();
        if (w.material.empty()) return std::nullopt;
        for (const CellRef& ref : frame.cells) {
            auto it = cells.find(ref.stem);
            if (it == cells.end() || !it->second.ok) continue;
            const GpuCell& g = it->second;
            for (std::size_t i = 0; i < g.surface_y.size(); ++i)
                if (g.surface_y[i] && g.mesh->materials[i].unpictured == w.material)
                    return *g.surface_y[i] + w.plane_offset * 256.0f;
        }
        return std::nullopt;
    }

    // The surface's plane in view space: its unit normal (the world's y axis) and distance.
    static std::array<float, 4> view_plane(const Camera& cam, float height) {
        const double n[3] = {cam.rot[1], cam.rot[4], cam.rot[7]};
        const double d = height + n[0] * cam.trans[0] + n[1] * cam.trans[1] + n[2] * cam.trans[2];
        return {static_cast<float>(n[0]), static_cast<float>(n[1]), static_cast<float>(n[2]), static_cast<float>(d)};
    }

    WaterBlock water_block(const Frame& frame, const View& view) const {
        WaterBlock b;
        const WaterSurface& w = water_surface();
        const Camera& cam = frame.camera;
        for (int r = 0; r < 3; ++r) {
            double t = 0;
            for (int c = 0; c < 3; ++c) {
                b.world[static_cast<std::size_t>(c * 4 + r)] = static_cast<float>(cam.rot[static_cast<std::size_t>(c * 3 + r)]);
                t -= cam.rot[static_cast<std::size_t>(c * 3 + r)] * cam.trans[static_cast<std::size_t>(c)];
            }
            b.world[static_cast<std::size_t>(12 + r)] = static_cast<float>(t);
        }
        b.world[15] = 1.0f;
        b.plane = view_plane(cam, water_plane(frame).value_or(0.0f));
        const float cx = frame.geom_ofs_x + view.draw_ofs_x, cy = frame.geom_ofs_y + view.draw_ofs_y;
        b.proj = {view.ortho[0] * frame.h, view.ortho[5] * frame.h, view.ortho[0] * cx + view.ortho[12],
                  view.ortho[5] * cy + view.ortho[13]};
        const WaterQuality& q = water_quality();
        float phase = water_phase();
        if (q.phase_steps > 0) phase = std::floor(phase * static_cast<float>(q.phase_steps)) / static_cast<float>(q.phase_steps);
        b.base = {w.base_color[0], w.base_color[1], w.base_color[2], phase};
        b.lit = {w.lit_color[0], w.lit_color[1], w.lit_color[2], w.wave_contrast};
        const bool there = reflect_valid && reflect_serial == frame.serial && !mirror && reflect_w > 0;
        // The nearest filter shows the mirrored picture's own pixels, unblurred.
        const bool filtered = q.reflection_smooth && q.blur_taps > 1;
        const float aspect = view.pixel_w > 0 ? view.pixel_h / view.pixel_w : 0.75f;
        const float wave_length = std::max(w.wave_length, 0.01f);
        b.level = {static_cast<float>(q.wave_layers), q.wave_grid > 0.0f ? 6.2831853f / (wave_length * q.wave_grid) : 0.0f,
                   static_cast<float>(q.color_steps), filtered ? static_cast<float>(q.blur_taps) : 1.0f};
        b.wave = {6.2831853f / (wave_length * 256.0f), w.wave_distortion * q.distortion,
                  there && filtered ? w.reflection_blur / static_cast<float>(reflect_w) : 0.0f,
                  there && filtered ? w.reflection_blur / static_cast<float>(reflect_h) : 0.0f};
        b.tint = {w.reflection_tint[0] * w.reflection_strength, w.reflection_tint[1] * w.reflection_strength,
                  w.reflection_tint[2] * w.reflection_strength, there ? 1.0f : 0.0f};
        b.glow = {w.glow_color[0] * w.glow_strength, w.glow_color[1] * w.glow_strength, w.glow_color[2] * w.glow_strength,
                  aspect};
        b.glow_size = {24.0f * w.glow_size * std::abs(view.ortho[0]), 48.0f * w.glow_size * std::abs(view.ortho[5]), 0, 0};
        if (frame.water_glow > 0.0f) {
            double v[3];
            for (std::size_t r = 0; r < 3; ++r) {
                v[r] = cam.trans[r];
                for (std::size_t c = 0; c < 3; ++c) v[r] += cam.rot[r * 3 + c] * frame.water_lamp[c];
            }
            const double side = b.plane[0] * v[0] + b.plane[1] * v[1] + b.plane[2] * v[2] - b.plane[3];
            for (std::size_t r = 0; r < 3; ++r) b.lamp[r] = static_cast<float>(v[r] - 2.0 * side * b.plane[r]);
            b.lamp[3] = frame.water_glow;
        }
        return b;
    }

    bool ensure_reflect_target(int width, int height) {
        if (reflect_color && reflect_w == width && reflect_h == height) return true;
        if (reflect_color) device.destroy(reflect_color);
        if (reflect_depth) device.destroy(reflect_depth);
        reflect_color = reflect_depth = {};
        reflect_w = reflect_h = 0;
        const auto w = static_cast<std::uint32_t>(width), h = static_cast<std::uint32_t>(height);
        auto color = device.createTexture({TextureFormat::RGBA8, w, h, kUsageSampled | kUsageColorTarget, "water mirror"});
        if (!color) {
            log(1, "[SCENE] water mirror target: " + color.error());
            return false;
        }
        auto depth = device.createTexture({TextureFormat::D24S8, w, h, kUsageDepthStencilTarget, "water mirror depth"});
        if (!depth) {
            device.destroy(color.value());
            log(1, "[SCENE] water mirror depth: " + depth.error());
            return false;
        }
        reflect_color = color.value();
        reflect_depth = depth.value();
        reflect_w = width;
        reflect_h = height;
        return true;
    }

    Result<void> ensure_buffer(BufferH& buf, std::uint32_t& cap, std::uint32_t need, BufferUsage usage) {
        if (buf && cap >= need) return Result<void>::ok();
        std::uint32_t size = std::max<std::uint32_t>(cap, 64u << 10);
        while (size < need) size *= 2;
        if (buf) device.destroy(buf);
        buf = {};
        cap = 0;
        auto b = device.createBuffer({usage, size, "scene prelit", device.caps().storageBuffers});
        if (!b) return Result<void>::fail(b.error());
        buf = b.value();
        cap = size;
        return Result<void>::ok();
    }
};

SceneRenderer::SceneRenderer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SceneRenderer::~SceneRenderer() = default;

Result<std::unique_ptr<SceneRenderer>> SceneRenderer::create(Device& device, TextureFormat color_format) {
    using R = Result<std::unique_ptr<SceneRenderer>>;
    auto impl = std::make_unique<Impl>(device, color_format);
    const auto vsrc = shaders::sceneVert();
    const auto fsrc = shaders::sceneFrag();
    if (spirvBackend(device.backend()) && (vsrc.spirv.empty() || fsrc.spirv.empty()))
        return R::fail("scene shaders: no SPIR-V in this build");
    ShaderDesc vd;
    vd.stage = ShaderStage::Vertex;
    vd.glsl = vsrc.glsl;
    vd.spirv = vsrc.spirv;
    vd.uniformBlockNames = kVsBlocks;
    vd.name = "scene.vert";
    auto vs = device.createShader(vd);
    if (!vs) return R::fail(vs.error());
    impl->vs = vs.value();
    ShaderDesc fd;
    fd.stage = ShaderStage::Fragment;
    fd.glsl = fsrc.glsl;
    fd.spirv = fsrc.spirv;
    fd.samplerNames = kFsSamplers;
    fd.uniformBlockNames = kFsBlocks;
    fd.name = "scene.frag";
    auto fs = device.createShader(fd);
    if (!fs) return R::fail(fs.error());
    impl->fs = fs.value();
    // The exact-depth variant: the same text with one define after its #version line.
    const auto fsrc_exact = shaders::sceneFragExactDepth();
    std::string exact_glsl(fsrc.glsl);
    const std::size_t first_line = exact_glsl.find('\n');
    exact_glsl.insert(first_line == std::string::npos ? 0 : first_line + 1, "#define SH_SCENE_EXACT_DEPTH 1\n");
    fd.glsl = exact_glsl;
    fd.spirv = fsrc_exact.spirv;
    fd.name = "scene.frag exact depth";
    auto fs_exact = device.createShader(fd);
    if (!fs_exact) return R::fail(fs_exact.error());
    impl->fs_exact = fs_exact.value();
    // The colour pipelines of a phone share the shadow filters and the map lights over 2x2 quads (scene.frag
    // SH_SCENE_QUAD, RENDER.md 7.14), where the device has quad operations in the fragment stage. SH_QUAD_SHADING=0
    // or 1 in the environment overrides the platform's choice, to compare.
    bool quad = false;
#if defined(__ANDROID__)
    quad = true;
#endif
    if (const char* env = std::getenv("SH_QUAD_SHADING"); env && *env) quad = env[0] == '1';
    ShaderH colour_fs = impl->fs_exact;
    const auto fsrc_quad = shaders::sceneFragQuad();
    if (quad && device.caps().fragmentQuadOps && !fsrc_quad.spirv.empty()) {
        fd.glsl = {};
        fd.spirv = fsrc_quad.spirv;
        fd.name = "scene.frag quad";
        if (auto fs_quad = device.createShader(fd)) {
            impl->fs_quad = fs_quad.value();
            colour_fs = impl->fs_quad;
        }
    }
    log(0, colour_fs == impl->fs_quad ? "[SCENE] shadows and map lights shared over 2x2 quads"
                                      : "[SCENE] shadows and map lights per pixel");

    for (int kind = 0; kind < kPipelineKinds; ++kind) {
        const int cull = kind != kNoCull;
        for (int blend = 0; blend < kBlendCount; ++blend) {
            PipelineDesc p;
            p.vertexShader = impl->vs;
            p.fragmentShader = colour_fs;
            p.vertexLayout = {sizeof(GpuVertex), kAttributes};
            p.cull = cull ? CullMode::Back : CullMode::None;
            p.frontFace = FrontFace::CounterClockwise;
            p.colorFormat = color_format;
            p.hasDepthStencil = true;
            p.depthStencil.depthTest = true;
            p.depthStencil.depthCompare = CompareOp::LessEqual;
            p.depthStencil.depthWrite = blend == 0;
            if (blend == 1) {
                // dst * src alpha + src: STP texels add, the shader gives the others alpha 0 (drawn solid).
                p.blend.enable = true;
                p.blend.srcColor = BlendFactor::One;
                p.blend.dstColor = BlendFactor::SrcAlpha;
                p.blend.srcAlpha = BlendFactor::Zero;
                p.blend.dstAlpha = BlendFactor::One;
            } else if (blend == static_cast<int>(Blend::Average) || blend == static_cast<int>(Blend::Mean)) {
                // Mean: the shader gives STP texels alpha 0.5 and the others 1 (drawn solid).
                p.blend.enable = true;
                p.blend.srcColor = BlendFactor::SrcAlpha;
                p.blend.dstColor = BlendFactor::OneMinusSrcAlpha;
                p.blend.srcAlpha = BlendFactor::Zero;
                p.blend.dstAlpha = BlendFactor::One;
            } else if (blend == static_cast<int>(Blend::Subtractive)) {
                // dst - src: no factor keeps a texel solid, so only STP texels may reach this pipeline.
                p.blend.enable = true;
                p.blend.srcColor = BlendFactor::One;
                p.blend.dstColor = BlendFactor::One;
                p.blend.colorOp = BlendOp::ReverseSubtract;
                p.blend.srcAlpha = BlendFactor::Zero;
                p.blend.dstAlpha = BlendFactor::One;
            } else if (blend == static_cast<int>(Blend::AddQuarter)) {
                // dst + src * 0.25 (the shader's alpha); STP texels only, like Subtractive.
                p.blend.enable = true;
                p.blend.srcColor = BlendFactor::SrcAlpha;
                p.blend.dstColor = BlendFactor::One;
                p.blend.srcAlpha = BlendFactor::Zero;
                p.blend.dstAlpha = BlendFactor::One;
            }
            p.blend.writeMask = kWriteR | kWriteG | kWriteB;
            p.name = "scene";
            impl->descs[kind][blend] = p;
            auto pipe = device.createPipeline(p);
            if (!pipe) return R::fail(pipe.error());
            impl->pipelines[kind][blend] = pipe.value();
            // A backend without line fill keeps the filled pipeline (pick falls back to it).
            p.wireframe = true;
            p.name = "scene wireframe";
            if (auto wire = device.createPipeline(p)) impl->wire_pipelines[kind][blend] = wire.value();
        }
    }
    {
        PipelineDesc p;
        p.vertexShader = impl->vs;
        p.fragmentShader = impl->fs;
        p.vertexLayout = {sizeof(GpuVertex), kAttributes};
        p.colorFormat = color_format;
        p.hasDepthStencil = true;
        p.depthStencil.depthTest = true;
        p.depthStencil.depthWrite = true;
        p.depthStencil.depthCompare = CompareOp::LessEqual;
        p.blend.writeMask = 0;
        p.name = "scene casters";
        impl->caster_desc = p;
        auto pipe = device.createPipeline(p);
        if (!pipe) return R::fail(pipe.error());
        impl->caster = pipe.value();
    }
    {
        // Drawn after the depth layer of the same pass: a surface behind a solid caster changes nothing.
        PipelineDesc p;
        p.vertexShader = impl->vs;
        p.fragmentShader = impl->fs;
        p.vertexLayout = {sizeof(GpuVertex), kAttributes};
        p.colorFormat = color_format;
        p.hasDepthStencil = true;
        p.depthStencil.depthTest = true;
        p.depthStencil.depthWrite = false;
        p.depthStencil.depthCompare = CompareOp::LessEqual;
        p.blend.enable = true;
        p.blend.srcColor = BlendFactor::Zero;
        p.blend.dstColor = BlendFactor::SrcColor;
        p.blend.srcAlpha = BlendFactor::One;
        p.blend.dstAlpha = BlendFactor::One;
        p.blend.alphaOp = BlendOp::Min;
        p.name = "scene caster transmittance";
        impl->caster_tint_desc = p;
        auto pipe = device.createPipeline(p);
        if (!pipe) return R::fail(pipe.error());
        impl->caster_tint = pipe.value();
    }
    {
        PipelineDesc p;
        p.vertexShader = impl->vs;
        p.fragmentShader = colour_fs;
        p.vertexLayout = {sizeof(GpuVertex), kAttributes};
        p.primitive = PrimitiveType::LineList;
        p.cull = CullMode::None;
        p.colorFormat = color_format;
        p.hasDepthStencil = true;
        p.depthStencil.depthTest = true;
        p.depthStencil.depthWrite = true;
        p.depthStencil.depthCompare = CompareOp::LessEqual;
        p.blend.writeMask = kWriteR | kWriteG | kWriteB;
        p.name = "scene seams";
        auto pipe = device.createPipeline(p);
        if (!pipe) return R::fail(pipe.error());
        impl->seam = pipe.value();
        if (impl->fs_quad) {
            // The late seams (draw): drawn after the opaque surfaces with the rasterised depth and no depth write, so
            // the depth test drops the line fragments the surfaces cover before they are shaded.
            const auto fsrc_raster = shaders::sceneFragQuadRaster();
            ShaderDesc rd = fd;
            rd.glsl = {};
            rd.spirv = fsrc_raster.spirv;
            rd.name = "scene.frag quad rasterised depth";
            if (auto fs_raster = device.createShader(rd)) {
                impl->fs_quad_raster = fs_raster.value();
                p.fragmentShader = impl->fs_quad_raster;
                p.name = "scene seams late";
                p.depthStencil.depthWrite = false;
                if (auto late = device.createPipeline(p)) impl->seam_late = late.value();
            }
        }
    }
    {
        PipelineDesc p;
        p.vertexShader = impl->vs;
        p.fragmentShader = impl->fs_exact;
        p.vertexLayout = {sizeof(GpuVertex), kAttributes};
        p.cull = CullMode::None;
        p.colorFormat = color_format;
        p.hasDepthStencil = true;
        p.depthStencil.depthTest = true;
        p.depthStencil.depthWrite = true;
        p.depthStencil.depthCompare = CompareOp::Always;
        p.blend.writeMask = 0;
        p.name = "scene depth reset";
        auto pipe = device.createPipeline(p);
        if (!pipe) return R::fail(pipe.error());
        impl->depth_reset = pipe.value();
    }
    auto lin = device.createSampler({Filter::Linear, Filter::Linear, AddressMode::Repeat, AddressMode::Repeat});
    auto near = device.createSampler({Filter::Nearest, Filter::Nearest, AddressMode::Repeat, AddressMode::Repeat});
    if (!lin || !near) return R::fail("scene samplers");
    impl->linear = lin.value();
    impl->nearest = near.value();
    auto lin_c = device.createSampler({Filter::Linear, Filter::Linear, AddressMode::ClampToEdge, AddressMode::ClampToEdge});
    auto near_c = device.createSampler({Filter::Nearest, Filter::Nearest, AddressMode::ClampToEdge, AddressMode::ClampToEdge});
    if (!lin_c || !near_c) return R::fail("scene samplers");
    impl->linear_clamp = lin_c.value();
    impl->nearest_clamp = near_c.value();
    impl->upscale = std::make_unique<ModelUpscaler>(device);
    return R::ok(std::unique_ptr<SceneRenderer>(new SceneRenderer(std::move(impl))));
}

Result<void> SceneRenderer::prepare(CommandList& cmd, const Frame& frame) {
    Impl& im = *impl_;
    if (!im.white) {
        auto w = im.device.createTexture({TextureFormat::RGBA8, 1, 1, kUsageSampled, "scene white"});
        if (!w) return Result<void>::fail(w.error());
        im.white = w.value();
        const std::uint8_t px[4] = {255, 255, 255, 255};
        cmd.uploadTexture(im.white, {0, 0, 1, 1}, std::as_bytes(std::span(px)));
    }
    if (!im.black) {
        auto b = im.device.createTexture({TextureFormat::RGBA8, 1, 1, kUsageSampled, "scene black"});
        if (!b) return Result<void>::fail(b.error());
        im.black = b.value();
        const std::uint8_t px[4] = {0, 0, 0, 255};
        cmd.uploadTexture(im.black, {0, 0, 1, 1}, std::as_bytes(std::span(px)));
    }
    if (!im.highlight) {
        auto h = im.device.createTexture({TextureFormat::RGBA8, 1, 1, kUsageSampled, "scene highlight"});
        if (!h) return Result<void>::fail(h.error());
        im.highlight = h.value();
        const std::uint8_t px[4] = {255, 40, 220, 255};
        cmd.uploadTexture(im.highlight, {0, 0, 1, 1}, std::as_bytes(std::span(px)));
    }
    ++im.frame_no;
    if (im.asset_generation != asset_generation()) {
        im.drop_assets();
        im.asset_generation = asset_generation();
    }
    if (im.release_generation != release_generation()) {
        im.drop_released();
        im.release_generation = release_generation();
    }
    if (const RefreshedTextures refreshed = take_refreshed_textures(); !refreshed.empty()) im.drop_refreshed(refreshed);
    // The textures that new meshes and those waiting for a better picture draw with are decoded together.
    std::vector<std::pair<std::string, int>> wanted;
    auto want = [&](const CellMesh& mesh) {
        for (const CellMaterial& m : mesh.materials)
            if (!m.texture.empty() && !im.textures.count({m.texture, m.palette}))
                wanted.emplace_back(m.texture, m.palette);
    };
    for (const CellRef& ref : frame.cells) {
        auto it = im.cells.find(ref.stem);
        if (it == im.cells.end()) {
            if (const auto mesh = find_cell(ref.stem)) want(*mesh);
        } else if (it->second.ok && it->second.textures_pending && it->second.mesh) {
            want(*it->second.mesh);
        }
    }
    for (const ObjectRef& o : frame.objects) {
        if (!o.mesh) continue;
        auto it = im.models.find(o.mesh.get());
        if (it == im.models.end() || (it->second.ok && it->second.textures_pending)) want(*o.mesh);
    }
    if (wanted.size() > 1) prefetch_textures(wanted);
    for (const CellRef& ref : frame.cells) {
        auto [it, added] = im.cells.try_emplace(ref.stem);
        if (!added) {
            if (it->second.ok && it->second.textures_pending) im.resolve_textures(cmd, it->second);
            continue;
        }
        it->second.mesh = find_cell(ref.stem);
        if (!it->second.mesh) {
            ++stats_.cells_missing;
            continue;
        }
        if (auto r = im.make_cell(cmd, it->second); !r) log(1, "[SCENE] cell " + ref.stem + ": " + r.error());
    }

    const UpscaleMode upscale = im.upscale->begin_frame(sh::render::graphics().model_upscaling);
    // The map's curved surfaces: a most level per cell by the size of its curved edges at its nearest point (the
    // eye's distance to its box), the frame's patches within kUpscaleMapBudget; a far cell is drawn plain.
    im.cell_levels.assign(frame.cells.size(), 0);
    if (upscale != UpscaleMode::Off) {
        const Camera& cam = frame.camera;
        double eye[3];
        for (int c = 0; c < 3; ++c) {
            eye[c] = 0;
            for (int r = 0; r < 3; ++r)
                eye[c] -= cam.rot[static_cast<std::size_t>(r * 3 + c)] * cam.trans[static_cast<std::size_t>(r)];
        }
        std::vector<PnModel> models;
        std::vector<std::size_t> which;
        for (std::size_t i = 0; i < frame.cells.size(); ++i) {
            const CellRef& ref = frame.cells[i];
            auto it = im.cells.find(ref.stem);
            if (it == im.cells.end() || !it->second.ok) continue;
            GpuCell& g = it->second;
            if (!g.patch_tried) im.make_cell_patches(cmd, g);
            if (!g.up_vertices) continue;
            const double origin[3] = {static_cast<double>(ref.cell_x) * kCellUnits, 0.0,
                                      static_cast<double>(ref.cell_z) * kCellUnits};
            double d2 = 0;
            for (int c = 0; c < 3; ++c) {
                const double lo = origin[c] + g.box_lo[static_cast<std::size_t>(c)];
                const double hi = origin[c] + g.box_hi[static_cast<std::size_t>(c)];
                const double d = eye[c] < lo ? lo - eye[c] : (eye[c] > hi ? eye[c] - hi : 0.0);
                d2 += d * d;
            }
            models.push_back({g.mean_edge, std::max(std::sqrt(d2), 16.0), g.curved_triangles});
            which.push_back(i);
        }
        const std::vector<int> levels = pn_levels(models, frame.h, im.px_per_unit, kUpscaleMapBudget);
        for (std::size_t k = 0; k < which.size(); ++k) im.cell_levels[which[k]] = levels[k];
    }
    for (const ObjectRef& o : frame.objects) {
        if (!o.mesh) continue;
        auto [it, added] = im.models.try_emplace(o.mesh.get());
        if (added) {
            it->second.mesh = o.mesh;
            if (auto r = im.make_cell(cmd, it->second); !r) log(1, "[SCENE] model " + o.mesh->stem + ": " + r.error());
        } else if (it->second.ok && it->second.textures_pending) {
            im.resolve_textures(cmd, it->second);
        }
        if (upscale != UpscaleMode::Off && it->second.ok && !it->second.patch_tried)
            im.make_patch_normals(cmd, it->second);
    }

    // Posed meshes: one stream for the frame.
    im.prelit_draws.clear();
    im.scratch_vertices.clear();
    im.scratch_indices.clear();
    im.scratch_patch_normals.clear();
    for (const PrelitMesh& m : frame.prelit) {
        PrelitDraw d;
        d.first = static_cast<std::uint32_t>(im.scratch_indices.size());
        d.blend = m.blend;
        d.split_stp = m.split_stp || m.blend == Blend::Subtractive || m.blend == Blend::AddQuarter;
        d.all_stp = m.all_stp;
        d.casts_shadow = m.casts_shadow;
        d.isolated = m.isolated;
        d.depth_reset = m.isolated && m.depth_reset;
        d.h = m.h;
        d.geom_ofs_x = m.geom_ofs_x;
        d.geom_ofs_y = m.geom_ofs_y;
        const std::uint32_t base = static_cast<std::uint32_t>(im.scratch_vertices.size());
        for (const PrelitVertex& v : m.vertices) {
            d.bounds.add(v.x, v.y, v.z);
            const auto fade = static_cast<std::uint8_t>(std::clamp(m.fade, 0.0f, 1.0f) * 255.0f + 0.5f);
            im.scratch_vertices.push_back(
                {v.x, v.y, v.z, 0, 0, 0, v.u, v.v, v.r, v.g, v.b, fade, {v.box_u0, v.box_v0, v.box_u1, v.box_v1}});
        }
        for (std::uint32_t i : m.indices) im.scratch_indices.push_back(base + i);
        d.count = static_cast<std::uint32_t>(m.indices.size());
        if (upscale != UpscaleMode::Off) {
            const bool patched = !m.isolated && m.patch_normals.size() == m.vertices.size();
            if (patched) {
                im.scratch_patch_normals.insert(im.scratch_patch_normals.end(), m.patch_normals.begin(),
                                                m.patch_normals.end());
            } else {
                im.scratch_patch_normals.resize(im.scratch_vertices.size(), {0.0f, 0.0f, 1.0f, 1.0f});
            }
            d.level = patched ? 1 : 0;
        }
        if (m.texture_key != 0) {
            if (auto it = im.prelit_textures.find(m.texture_key); it != im.prelit_textures.end()) {
                d.texture = it->second;
            } else if (const auto t = prelit_texture(m.texture_key); !t) {
                im.prelit_textures.emplace(m.texture_key, TextureH{});
            } else if (im.retry_due(im.prelit_retry, m.texture_key)) {
                auto up = im.upload_texture(cmd, *t, "scene prelit texture");
                if (up) {
                    im.prelit_retry.erase(m.texture_key);
                    im.prelit_textures.emplace(m.texture_key, up.value());
                    d.texture = up.value();
                } else {
                    im.upload_failed(im.prelit_retry, m.texture_key, std::string("posed mesh texture"), up.error());
                }
            }
        }
        im.prelit_draws.push_back(d);
    }
    if (!im.scratch_indices.empty()) {
        const auto vbytes = static_cast<std::uint32_t>(im.scratch_vertices.size() * sizeof(GpuVertex));
        const auto ibytes = static_cast<std::uint32_t>(im.scratch_indices.size() * 4);
        if (auto r = im.ensure_buffer(im.prelit_vertices, im.prelit_vertex_cap, vbytes, BufferUsage::Vertex); !r) return r;
        if (auto r = im.ensure_buffer(im.prelit_indices, im.prelit_index_cap, ibytes, BufferUsage::Index); !r) return r;
        cmd.uploadBuffer(im.prelit_vertices, 0, std::as_bytes(std::span(im.scratch_vertices)));
        cmd.uploadBuffer(im.prelit_indices, 0, std::as_bytes(std::span(im.scratch_indices)));
    }
    im.object_levels.assign(frame.objects.size(), 0);
    if (upscale != UpscaleMode::Off) im.upscale_levels(cmd, frame);
    return Result<void>::ok();
}

Result<void> SceneRenderer::draw(RenderPass& pass, const Frame& frame, const View& view, int item, Layer layer) {
    Impl& im = *impl_;
    const bool casters = layer != Layer::Color;
    // The shadow map layer in the material block (scene.frag u_mat.z).
    const float layer_id = layer == Layer::ShadowTint ? 2.0f : (casters ? 1.0f : 0.0f);
    if (item < 0 && !casters) {
        ++stats_.frames;
        stats_.lights = static_cast<std::uint32_t>(frame.lights.size());
    }
    const Camera& cam = frame.camera;
    const Lighting& light = frame.light;

    VsBlock vsb;
    vsb.ortho = view.ortho;
    vsb.proj = {frame.h, frame.geom_ofs_x + view.draw_ofs_x, frame.geom_ofs_y + view.draw_ofs_y, view.sz_max};
    vsb.light = {static_cast<float>(light.mode), light.ambient, light.max_intensity, light.depth_cue};
    vsb.tint = {light.tint[0], light.tint[1], light.tint[2], light.fog ? 1.0f : 0.0f};
    vsb.far_color = {light.far_color[0], light.far_color[1], light.far_color[2], 0};
    // The editor's viewer overrides: drawn only while the editor is open, never in the shadow
    // layers.
    const ViewerOverrides& vo = viewer_overrides();
    const bool viewer = vo.active && !casters;
    const bool wire = viewer && vo.wireframe;
    const bool full_bright = viewer && vo.full_bright;
    // The texture browser's highlight: the material's picture becomes the flat highlight colour.
    auto shown = [&](const CellMaterial& m, TextureH tex) {
        return viewer && !vo.highlight_texture.empty() && m.texture == vo.highlight_texture ? im.highlight : tex;
    };
    if (viewer) {
        if (full_bright) {
            const float level = 128.0f * vo.light_scale;
            vsb.light[0] = 0.0f;
            vsb.tint = {level, level, level, 0.0f};
        } else {
            if (vo.flashlight_off && light.mode == 1) {
                // The point light is the flashlight: what is left is the room's darkness toward its far colour.
                vsb.light[0] = 0.0f;
                const float a = light.ambient / 4096.0f;
                for (std::size_t i = 0; i < 3; ++i)
                    vsb.tint[i] = light.far_color[i] + (light.tint[i] - light.far_color[i]) * a;
            }
            for (std::size_t i = 0; i < 3; ++i) vsb.tint[i] *= vo.light_scale;
            if (vo.fog_off) vsb.tint[3] = 0.0f;
        }
    }
    for (int r = 0; r < 3; ++r) {
        double lv = 0, lp = cam.trans[static_cast<std::size_t>(r)];
        for (int c = 0; c < 3; ++c) {
            lv += cam.rot[static_cast<std::size_t>(r * 3 + c)] * light.light_vec[static_cast<std::size_t>(c)];
            lp += cam.rot[static_cast<std::size_t>(r * 3 + c)] * light.light_pos[static_cast<std::size_t>(c)];
        }
        vsb.light_vec[static_cast<std::size_t>(r)] = static_cast<float>(lv);
        vsb.light_pos[static_cast<std::size_t>(r)] = static_cast<float>(lp);
    }
    vsb.misc = {kNearPlane, kWorldDepthBias, casters ? 1.0f : 0.0f, 0};
    vsb.shadow_matrix = view.fs.shadowMatrix;

    const SamplerH sampler = view.fs.modes[2] > 0.5f ? im.linear : im.nearest;
    FogRampUniforms fog_ramp = view.fog;
    if (viewer && (vo.fog_off || full_bright)) {
        fog_ramp.params[0] = 0.0f;
        fog_ramp.params[3] = 0.0f;
    }
    pass.pushUniforms(ShaderStage::Vertex, 1, std::as_bytes(std::span(&fog_ramp, 1)));
    const bool shadows = !casters && view.fs.prog[3] > 0.5f && view.shadow_map;
    PrimFsUniforms fs = view.fs;
    if (!shadows) fs.prog[3] = 0.0f;
    if (viewer && (vo.fog_off || full_bright)) fs.fog[3] = 0.0f;
    if (viewer && (vo.flashlight_off || full_bright)) fs.flMode[0] = 0.0f;
    // The fog is the draw distance: in colour what lies wholly past it is left out, in a shadow layer what lies
    // wholly outside the light's frustum (RENDER.md 7.7). The fog as this draw shades it, the viewer's included.
    DrawCull cull;
    if (!casters) cull.cut_z = fog_draw_cutoff(fog_ramp, fs.fog[3], fs.modes[1] > 0.5f);
    cull.near_z = kNearPlane;
    cull.sides = {view.ortho[0] * frame.h, view.ortho[0] * (frame.geom_ofs_x + view.draw_ofs_x) + view.ortho[12],
                  view.ortho[5] * frame.h, view.ortho[5] * (frame.geom_ofs_y + view.draw_ofs_y) + view.ortho[13]};
    if (casters) cull.light = &view.fs.shadowMatrix;
    pass.pushUniforms(ShaderStage::Fragment, 0, std::as_bytes(std::span(&fs, 1)));
    pass.pushUniforms(ShaderStage::Fragment, 1, std::as_bytes(std::span(&fog_ramp, 1)));
    pass.bindSampler(ShaderStage::Fragment, 8, view.fog_ramp ? view.fog_ramp : im.black, im.nearest);
    // The water's block and mirrored picture, for the cells' water material.
    const std::string& water_material = water_surface().material;
    FrameBlock frame_block;
    frame_block.water = im.water_block(frame, view);
    const WaterBlock& water = frame_block.water;
    const bool sun_map = !casters && view.lights.info[2] > 0.5f && view.sun_shadow;
    if (!casters) {
        frame_block.lights = view.lights;
        // The tiles change only with the lights and the projection; a frame draws many items with the same ones.
        if (frame_block.lights.info[0] > 0.5f) {
            if (im.light_tiles_proj != water.proj || im.light_tiles_pos != view.lights.pos ||
                im.light_tiles_count != view.lights.info[0]) {
                fill_light_tiles(frame_block.lights, water.proj);
                im.light_tiles_proj = water.proj;
                im.light_tiles_pos = view.lights.pos;
                im.light_tiles_count = view.lights.info[0];
                im.light_tiles = frame_block.lights.tiles;
            } else {
                frame_block.lights.tiles = im.light_tiles;
            }
        }
    }
    if (!sun_map) frame_block.lights.info[2] = 0.0f;
    pass.pushUniforms(ShaderStage::Fragment, 3, std::as_bytes(std::span(&frame_block, 1)));
    pass.bindSampler(ShaderStage::Fragment, 7, sun_map ? view.sun_shadow : im.white,
                     sun_map && view.sun_sampler ? view.sun_sampler : im.nearest);
    pass.bindSampler(ShaderStage::Fragment, 3, water.tint[3] > 0.5f ? im.reflect_color : im.black,
                     water_quality().reflection_smooth ? im.linear_clamp : im.nearest_clamp);

    PipelineH bound;
    auto bind = [&](PipelineH p) {
        if (casters) p = layer == Layer::ShadowTint ? im.caster_tint : im.caster;
        if (p != bound) {
            pass.bindPipeline(p);
            bound = p;
        }
    };
    if (!casters && item < 0 && view.pixel_w > 0) im.px_per_unit = std::abs(view.ortho[0]) * view.pixel_w * 0.5f;
    // Binds the pipeline of (kind, blend), its upscaled twin where the model has a level above 1 (model_upscale.h;
    // `map`: a map surface's, always through tessellation). True for the twin.
    auto use = [&](int kind, int blend, int level, bool map = false) {
        if (level > 1 && im.upscale->mode() != UpscaleMode::Off) {
            const bool tint = layer == Layer::ShadowTint;
            const int slot =
                (casters ? (tint ? kUpscaleCasterTintSlot : kUpscaleCasterSlot) : kind * kBlendCount + blend) +
                (map ? kUpscaleMapSlots : 0) + (wire ? kUpscaleWireSlots : 0);
            PipelineDesc desc = casters ? (tint ? im.caster_tint_desc : im.caster_desc) : im.descs[kind][blend];
            if (wire) desc.wireframe = true;
            if (const PipelineH p = im.upscale->pipeline(slot, desc, map)) {
                if (p != bound) {
                    pass.bindPipeline(p);
                    bound = p;
                }
                return true;
            }
        }
        bind(im.pick(kind, blend, wire));
        return false;
    };
    auto issue = [&](bool upscaled, std::uint32_t first, std::uint32_t count, BufferH patch_normals, BufferH vertices,
                     BufferH indices, int level, bool map = false) {
        if (upscaled)
            im.upscale->draw(pass, {patch_normals, vertices, indices, first, count, level, map,
                                    static_cast<float>(frame.h) * im.px_per_unit});
        else
            pass.drawIndexed(count, first, 0);
    };
    // The shadow map the PS1 path drew its casters into; white (never in shadow) when there is none.
    pass.bindSampler(ShaderStage::Fragment, 1, shadows ? view.shadow_map : im.white,
                     shadows && view.shadow_sampler ? view.shadow_sampler : im.nearest);
    MatBlock last_mat;
    last_mat.mat = {-1, -1, -1, -1};
    // The frame's ray-traced results, at slots 4 to 6 (black where an effect is off).
    std::array<float, 4> rt{};
    if (!casters) {
        const TextureH rts[3] = {view.rt_shadow, view.rt_reflect, view.rt_gi};
        for (std::uint32_t i = 0; i < 3; ++i) {
            rt[i] = rts[i] ? 1.0f : 0.0f;
            // 2: the reflection traced mirror rays, which may replace the water's mirrored picture.
            if (i == 1 && rts[i] && view.rt_reflect_mirrors) rt[i] = 2.0f;
            pass.bindSampler(ShaderStage::Fragment, 4 + i, rts[i] ? rts[i] : im.black, im.nearest);
        }
    }
    // w: texels drawn (0 all, 1 solid only, 2 STP only, 3 all as STP) plus 4 for an isolated model (item preview).
    auto material = [&](float blend, bool textured, TextureH tex, float texels = 0.0f, bool isolated = false,
                        bool is_water = false) {
        MatBlock m;
        m.mat = {blend, is_water ? 2.0f : (textured ? 1.0f : 0.0f), layer_id, texels + (isolated ? 4.0f : 0.0f)};
        m.rt = rt;
        if (tex)
            if (auto it = im.mip_levels.find(tex.raw); it != im.mip_levels.end())
                m.mat2[0] = static_cast<float>(it->second);
        m.mat2[1] = last_mat.mat2[1];
        m.mat2[2] = last_mat.mat2[2];
        if (m.mat != last_mat.mat || m.mat2 != last_mat.mat2) {
            pass.pushUniforms(ShaderStage::Fragment, 2, std::as_bytes(std::span(&m, 1)));
            last_mat = m;
        }
        pass.bindSampler(ShaderStage::Fragment, 0, tex ? tex : im.white, sampler);
    };
    // The map lights that can reach the next draws (draw_light_mask), into the material block.
    const bool lit = !casters && frame_block.lights.info[0] > 0.5f;
    auto light_mask = [&](const Bounds& box, const std::array<float, 16>& mv) {
        return lit ? draw_light_mask(frame_block.lights, box, mv) : ~0u;
    };
    auto use_lights = [&](std::uint32_t mask) {
        const std::array<float, 2> v = {static_cast<float>(mask & 0xFFFFu), static_cast<float>(mask >> 16)};
        if (v[0] == last_mat.mat2[1] && v[1] == last_mat.mat2[2]) return;
        last_mat.mat2[1] = v[0];
        last_mat.mat2[2] = v[1];
        // Before the first material the block goes out with it.
        if (last_mat.mat[0] >= 0.0f)
            pass.pushUniforms(ShaderStage::Fragment, 2, std::as_bytes(std::span(&last_mat, 1)));
    };
    pass.bindSampler(ShaderStage::Fragment, 2, shadows && view.shadow_tint ? view.shadow_tint : im.white, im.linear);
    // Every blended texel casts into the transmittance layer (scene.frag weighs it by its blend).
    auto dims_light = [](Blend b) { return b != Blend::Opaque; };
    // Which pass of a cell or object material a layer draws: the shadow depth takes the first (the solid texels),
    // the transmittance the second (the blended texels).
    auto skip_range = [&](int semi, const CellMaterial& m) {
        if (semi == 1 && !m.semi_transparent) return true;
        if (layer == Layer::ShadowDepth) return semi == 1;
        if (layer == Layer::ShadowTint) return semi == 0 || !dims_light(m.blend);
        return false;
    };

    // Cells: modelView = R * (p + placement) + T, composed in double so far cells keep their precision.
    auto cell_matrix = [&](const CellRef& ref, std::array<float, 16>& mv) {
        const double o[3] = {static_cast<double>(ref.cell_x) * kCellUnits, 0.0, static_cast<double>(ref.cell_z) * kCellUnits};
        for (int r = 0; r < 3; ++r) {
            double t = cam.trans[static_cast<std::size_t>(r)];
            for (int c = 0; c < 3; ++c) {
                const double m = cam.rot[static_cast<std::size_t>(r * 3 + c)];
                mv[static_cast<std::size_t>(c * 4 + r)] = static_cast<float>(m);
                t += m * o[c];
            }
            mv[static_cast<std::size_t>(12 + r)] = static_cast<float>(t);
        }
        mv[3] = mv[7] = mv[11] = 0.0f;
        mv[15] = 1.0f;
    };

    // The pipeline kind of a cell or object range, with the range's overlay lift made current. The shadow map takes
    // every layer as it is: a lift there would only move the caster.
    auto layer_kind = [&](const CellRange& r) {
        const float lift = casters ? 0.0f : static_cast<float>(r.layer) * kOverlayLift;
        if (lift != vsb.misc[3]) {
            vsb.misc[3] = lift;
            pass.pushUniforms(ShaderStage::Vertex, 0, std::as_bytes(std::span(&vsb, 1)));
        }
        return r.layer > 0 ? kOverlay : kCullBack;
    };

    if (item >= kObjectItem) {
        // One library model where the game placed it, at the point of the frame the game sorted it to: the cells'
        // light, fog and flashlight, without the map's depth bias (a prop stands on the map and wins against it).
        const std::size_t index = static_cast<std::size_t>(item - kObjectItem);
        if (index >= frame.objects.size()) return Result<void>::ok();
        const ObjectRef& o = frame.objects[index];
        auto it = o.mesh ? im.models.find(o.mesh.get()) : im.models.end();
        if (it == im.models.end() || !it->second.ok) return Result<void>::ok();
        const GpuCell& g = it->second;
        // modelView = R * (W * p + t) + T, composed in double.
        for (int r = 0; r < 3; ++r) {
            double t = cam.trans[static_cast<std::size_t>(r)];
            for (int c = 0; c < 3; ++c) {
                double m = 0;
                for (int k = 0; k < 3; ++k)
                    m += cam.rot[static_cast<std::size_t>(r * 3 + k)] * o.rot[static_cast<std::size_t>(k * 3 + c)];
                vsb.model_view[static_cast<std::size_t>(c * 4 + r)] = static_cast<float>(m);
                t += cam.rot[static_cast<std::size_t>(r * 3 + c)] * o.trans[static_cast<std::size_t>(c)];
            }
            vsb.model_view[static_cast<std::size_t>(12 + r)] = static_cast<float>(t);
        }
        vsb.model_view[3] = vsb.model_view[7] = vsb.model_view[11] = 0.0f;
        vsb.model_view[15] = 1.0f;
        if (cull.out(g.bounds, vsb.model_view)) return Result<void>::ok();
        use_lights(light_mask(g.bounds, vsb.model_view));
        vsb.misc[1] = 0.0f;
        vsb.misc[3] = 0.0f;
        // The light rules take unit normals: a scaled model (an item model drawn in the world) gives its scale back.
        const double scale = std::sqrt(o.rot[0] * o.rot[0] + o.rot[3] * o.rot[3] + o.rot[6] * o.rot[6]);
        vsb.seam[2] = scale > 1e-9 && std::abs(scale - 1.0) > 1e-4 ? static_cast<float>(1.0 / scale) : 0.0f;
        pass.pushUniforms(ShaderStage::Vertex, 0, std::as_bytes(std::span(&vsb, 1)));
        pass.bindVertexBuffer(g.vertices, 0);
        pass.bindIndexBuffer(g.indices, IndexType::U32, 0);
        const int level = index < im.object_levels.size() ? im.object_levels[index] : 0;
        for (int semi = 0; semi < 2; ++semi) {
            for (const CellRange& r : g.mesh->ranges) {
                const CellMaterial& m = g.mesh->materials[static_cast<std::size_t>(r.material)];
                if (skip_range(semi, m)) continue;
                const int b = semi == 1 ? static_cast<int>(m.blend) : 0;
                const bool up = use(layer_kind(r), b, level);
                const TextureH tex = shown(m, g.textures[static_cast<std::size_t>(r.material)]);
                const float texels = m.semi_transparent ? (semi == 1 ? 2.0f : 1.0f) : 0.0f;
                material(static_cast<float>(b), static_cast<bool>(tex), tex, texels);
                issue(up, r.first, r.count, g.patch_normals, g.vertices, g.indices, level);
                ++stats_.object_draws;
            }
        }
        return Result<void>::ok();
    }

    if (item >= 0) {
        // One posed mesh, at the point of the frame the game sorted it to.
        if (static_cast<std::size_t>(item) >= im.prelit_draws.size()) return Result<void>::ok();
        const PrelitDraw& d = im.prelit_draws[static_cast<std::size_t>(item)];
        if (d.count < 3 || (casters && (!d.casts_shadow || d.depth_reset))) return Result<void>::ok();
        // Shadow layers: the solid texels into the depth (alpha blending has none), the blended into the transmittance.
        if (layer == Layer::ShadowDepth && d.blend != Blend::Opaque && (d.all_stp || d.blend == Blend::Average))
            return Result<void>::ok();
        if (layer == Layer::ShadowTint && !dims_light(d.blend)) return Result<void>::ok();
        VsBlock pv = vsb;
        pv.model_view = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        if (im.mirror) pv.model_view = *im.mirror;
        if (!d.isolated && cull.out(d.bounds, pv.model_view)) return Result<void>::ok();
        use_lights(d.isolated ? ~0u : light_mask(d.bounds, pv.model_view));
        // Full bright: the posed meshes lose their own light too and take the flat level (an isolated item preview
        // keeps it).
        pv.light[0] = full_bright && !d.isolated ? 0.0f : 3.0f;
        pv.misc[1] = 0.0f;
        if (d.isolated)
            pv.proj = {d.h, d.geom_ofs_x + view.draw_ofs_x, d.geom_ofs_y + view.draw_ofs_y, view.sz_max};
        pass.pushUniforms(ShaderStage::Vertex, 0, std::as_bytes(std::span(&pv, 1)));
        pass.bindVertexBuffer(im.prelit_vertices, 0);
        pass.bindIndexBuffer(im.prelit_indices, IndexType::U32, 0);
        const int b = static_cast<int>(d.blend);
        auto prelit = [&](bool up) {
            issue(up, d.first, d.count, im.prelit_patch_normals, im.prelit_vertices, im.prelit_indices, d.level);
        };
        if (casters) {
            const bool tint = layer == Layer::ShadowTint;
            const bool up = use(0, 0, d.level);
            const float texels = tint ? (d.all_stp ? 3.0f : (d.blend == Blend::Average ? 0.0f : 2.0f)) : (b == 0 ? 0.0f : 1.0f);
            material(tint ? static_cast<float>(b) : 0.0f, static_cast<bool>(d.texture), d.texture, texels);
            prelit(up);
            return Result<void>::ok();
        }
        if (d.depth_reset) {
            bind(im.depth_reset);
            material(static_cast<float>(b), static_cast<bool>(d.texture), d.texture, 0.0f, d.isolated);
            pass.drawIndexed(d.count, d.first, 0);
            ++stats_.prelit_draws;
            return Result<void>::ok();
        }
        if (!d.split_stp || b == 0) {
            const bool up = use(0, b, d.level);
            material(static_cast<float>(b), static_cast<bool>(d.texture), d.texture, 0.0f, d.isolated);
            prelit(up);
            ++stats_.prelit_draws;
            return Result<void>::ok();
        }
        // Like a semi-transparent cell material: the solid texels with depth, then the STP texels blended.
        if (!d.all_stp) {
            const bool up = use(0, 0, d.level);
            material(0.0f, static_cast<bool>(d.texture), d.texture, 1.0f, d.isolated);
            prelit(up);
        }
        const bool up = use(0, b, d.level);
        material(static_cast<float>(b), static_cast<bool>(d.texture), d.texture, d.all_stp ? 3.0f : 2.0f, d.isolated);
        prelit(up);
        ++stats_.prelit_draws;
        return Result<void>::ok();
    }

    // Seams first: every triangle drawn after them at the same depth covers them, so only the cracks keep their colour.
    // The lines are drawn on their pixel and one pixel to each side: the gaps the data leaves between models that only
    // nearly meet are up to a few PS1 units, two or three pixels at 1080p.
    // With quad shading the seams come after the opaque surfaces (seam_late): nearly every line pixel lies under a
    // surface and is dropped by the depth test before its shader runs. They write no depth, and their rasterised
    // depth is as far behind as the setback of a line puts the exact one, so a crack shows them as before.
    const bool late_seams = static_cast<bool>(im.seam_late);
    auto draw_seams = [&](bool late) {
        const float px = view.pixel_w > 0 ? 2.0f / view.pixel_w : 0.0f;
        const float py = view.pixel_h > 0 ? 2.0f / view.pixel_h : 0.0f;
        const std::array<std::array<float, 2>, 5> shifts = {{{0, 0}, {px, 0}, {-px, 0}, {0, py}, {0, -py}}};
        const int passes = (px > 0 && py > 0) ? 5 : 1;
        bind(late ? im.seam_late : im.seam);
        for (int s = 0; s < passes; ++s) {
            vsb.seam = {shifts[static_cast<std::size_t>(s)][0], shifts[static_cast<std::size_t>(s)][1], 0,
                        late ? kWorldDepthBias * 8.0f : 0.0f};
            for (const CellRef& ref : frame.cells) {
                auto it = im.cells.find(ref.stem);
                if (it == im.cells.end() || !it->second.ok || it->second.mesh->seams.empty()) continue;
                const GpuCell& g = it->second;
                cell_matrix(ref, vsb.model_view);
                if (cull.out(g.bounds, vsb.model_view)) continue;
                use_lights(light_mask(g.bounds, vsb.model_view));
                pass.pushUniforms(ShaderStage::Vertex, 0, std::as_bytes(std::span(&vsb, 1)));
                pass.bindVertexBuffer(g.vertices, 0);
                pass.bindIndexBuffer(g.indices, IndexType::U32, 0);
                for (const CellRange& r : g.mesh->seams) {
                    const CellMaterial& m = g.mesh->materials[static_cast<std::size_t>(r.material)];
                    const TextureH tex = g.textures[static_cast<std::size_t>(r.material)];
                    material(0.0f, static_cast<bool>(tex), tex, 0.0f, false,
                             !m.unpictured.empty() && m.unpictured == water_material);
                    pass.drawIndexed(r.count, r.first, 0);
                }
            }
        }
        vsb.seam = {0, 0, 0, 0};
    };
    if (!casters && !late_seams) draw_seams(false);
    // A semi-transparent material is drawn in both passes: its solid texels in the first, with the opaque surfaces
    // and writing depth, its STP texels in the second, blended. On the PS1 the solid texels of such a polygon cover
    // whatever the ordering table drew before it; here the sprites and prims drawn after the scene (the bushes in the
    // pit under the grates of map6_s00) are only kept behind the grate by its depth.
    for (int semi = 0; semi < 2; ++semi) {
        if (semi == 1 && !casters && late_seams) draw_seams(true);
        for (std::size_t ci = 0; ci < frame.cells.size(); ++ci) {
            const CellRef& ref = frame.cells[ci];
            auto it = im.cells.find(ref.stem);
            if (it == im.cells.end() || !it->second.ok) continue;
            const GpuCell& g = it->second;
            cell_matrix(ref, vsb.model_view);
            if (cull.out(g.bounds, vsb.model_view)) continue;
            vsb.misc[3] = 0.0f;
            pass.pushUniforms(ShaderStage::Vertex, 0, std::as_bytes(std::span(&vsb, 1)));
            // Model upscaling: the cell's flat triangles plain, its curved ones as patches (make_cell_patches).
            const int cell_level = ci < im.cell_levels.size() ? im.cell_levels[ci] : 0;
            const bool curved = cell_level > 1 && g.up_vertices && g.up_ranges.size() == g.mesh->ranges.size() &&
                                im.upscale->mode() != UpscaleMode::Off;
            pass.bindVertexBuffer(curved ? g.up_vertices : g.vertices, 0);
            pass.bindIndexBuffer(curved ? g.up_indices : g.indices, IndexType::U32, 0);
            for (std::size_t ri = 0; ri < g.mesh->ranges.size(); ++ri) {
                const CellRange& r = g.mesh->ranges[ri];
                const CellMaterial& m = g.mesh->materials[static_cast<std::size_t>(r.material)];
                if (skip_range(semi, m)) continue;
                if (ri < g.range_bounds.size() && cull.out(g.range_bounds[ri], vsb.model_view)) continue;
                use_lights(ri < g.range_bounds.size() ? light_mask(g.range_bounds[ri], vsb.model_view)
                                                      : light_mask(g.bounds, vsb.model_view));
                const int b = semi == 1 ? static_cast<int>(m.blend) : 0;
                const int kind = layer_kind(r);
                bind(im.pick(kind, b, wire));
                const TextureH tex = shown(m, g.textures[static_cast<std::size_t>(r.material)]);
                const float texels = m.semi_transparent ? (semi == 1 ? 2.0f : 1.0f) : 0.0f;
                const bool is_water = !m.unpictured.empty() && m.unpictured == water_material;
                material(static_cast<float>(b), static_cast<bool>(tex), tex, texels, false, is_water);
                ++stats_.cell_draws;
                if (!curved) {
                    // The runs the fog or the light leaves in, neighbours reached by the same map lights as one draw.
                    std::uint32_t first = 0, count = 0, lights = 0;
                    for (const GpuCell::Chunk& c : g.range_chunks[ri]) {
                        if (cull.out(c.bounds, vsb.model_view)) continue;
                        const std::uint32_t mask = light_mask(c.bounds, vsb.model_view);
                        if (count > 0 && first + count == c.first && mask == lights) {
                            count += c.count;
                            continue;
                        }
                        if (count > 0) {
                            use_lights(lights);
                            pass.drawIndexed(count, first, 0);
                        }
                        first = c.first;
                        count = c.count;
                        lights = mask;
                    }
                    if (count > 0) {
                        use_lights(lights);
                        pass.drawIndexed(count, first, 0);
                    }
                    continue;
                }
                const GpuCell::UpRange& u = g.up_ranges[ri];
                if (u.flat > 0) pass.drawIndexed(u.flat * 3, u.first, 0);
                if (u.curved == 0) continue;
                // The shadow maps take a coarser cut: they only need the outline.
                const int level = casters ? std::min(cell_level, kUpscaleCasterLevel) : cell_level;
                const bool up = use(kind, b, level, true);
                material(static_cast<float>(b), static_cast<bool>(tex), tex, texels, false, is_water);
                issue(up, u.first + u.flat * 3, u.curved * 3, g.patch_normals, g.up_vertices, g.up_indices, level,
                      true);
            }
        }
    }
    return Result<void>::ok();
}

bool SceneRenderer::reflect(CommandList& cmd, const Frame& frame, const View& view) {
    Impl& im = *impl_;
    im.reflect_valid = false;
    const WaterSurface& w = water_surface();
    const int lines = water_quality().reflection_lines;
    if (lines <= 0 || w.reflection_strength <= 0.0f || view.pixel_w <= 0.0f || view.pixel_h <= 0.0f) return false;
    const std::optional<float> height = im.water_plane(frame);
    if (!height) return false;
    const auto mirrored = [](const PrelitDraw& d) { return d.count >= 3 && !d.isolated; };
    if (std::none_of(im.prelit_draws.begin(), im.prelit_draws.end(), mirrored)) return false;
    const int rows = std::clamp(lines, 16, 2160);
    const int columns = std::clamp(static_cast<int>(std::lround(rows * view.pixel_w / view.pixel_h)), 16, 4096);
    if (!im.ensure_reflect_target(columns, rows)) return false;

    RenderPassDesc desc;
    desc.color = {im.reflect_color, LoadOp::Clear, StoreOp::Store, {0.f, 0.f, 0.f, 1.f}};
    desc.depthStencil = {im.reflect_depth, LoadOp::Clear, StoreOp::DontCare, 1.f, 0};
    desc.name = "reflection";
    RenderPass* pass = cmd.beginRenderPass(desc);
    if (!pass) return false;
    pass->setViewport({0.f, 0.f, static_cast<float>(columns), static_cast<float>(rows), 0.f, 1.f});

    // v' = v - 2 (n.v - d) n, for the meshes and for the flashlight that lights them.
    const std::array<float, 4> plane = Impl::view_plane(frame.camera, *height);
    std::array<float, 16> m{};
    for (std::size_t c = 0; c < 3; ++c)
        for (std::size_t r = 0; r < 3; ++r) m[c * 4 + r] = (r == c ? 1.0f : 0.0f) - 2.0f * plane[r] * plane[c];
    for (std::size_t r = 0; r < 3; ++r) m[12 + r] = 2.0f * plane[3] * plane[r];
    m[15] = 1.0f;
    View mirror_view = view;
    mirror_view.pixel_w = static_cast<float>(columns);
    mirror_view.pixel_h = static_cast<float>(rows);
    mirror_view.shadow_map = {};
    mirror_view.shadow_tint = {};
    mirror_view.lights = {};
    mirror_view.sun_shadow = {};
    PrimFsUniforms& fs = mirror_view.fs;
    fs.prog[3] = 0.0f;
    const float pos_side = plane[0] * fs.flPos[0] + plane[1] * fs.flPos[1] + plane[2] * fs.flPos[2] - plane[3];
    const float dir_side = plane[0] * fs.flDir[0] + plane[1] * fs.flDir[1] + plane[2] * fs.flDir[2];
    for (std::size_t k = 0; k < 3; ++k) {
        fs.flPos[k] -= 2.0f * pos_side * plane[k];
        fs.flDir[k] -= 2.0f * dir_side * plane[k];
    }

    im.mirror = m;
    const Stats kept = stats_;
    for (std::size_t i = 0; i < im.prelit_draws.size(); ++i)
        if (mirrored(im.prelit_draws[i])) (void)draw(*pass, frame, mirror_view, static_cast<int>(i));
    stats_ = kept;
    im.mirror.reset();
    cmd.endRenderPass();
    im.reflect_valid = true;
    im.reflect_serial = frame.serial;
    return true;
}

namespace {

// Row-major 3x4 of model to view: R * (W * p + t) + T, composed in double like the draws' model-view matrices.
std::array<float, 12> ray_transform(const Camera& cam, const std::array<double, 9>& w, const std::array<double, 3>& t) {
    std::array<float, 12> out{};
    for (int r = 0; r < 3; ++r) {
        double tr = cam.trans[static_cast<std::size_t>(r)];
        for (int c = 0; c < 3; ++c) {
            double m = 0;
            for (int k = 0; k < 3; ++k)
                m += cam.rot[static_cast<std::size_t>(r * 3 + k)] * w[static_cast<std::size_t>(k * 3 + c)];
            out[static_cast<std::size_t>(r * 4 + c)] = static_cast<float>(m);
            tr += cam.rot[static_cast<std::size_t>(r * 3 + c)] * t[static_cast<std::size_t>(c)];
        }
        out[static_cast<std::size_t>(r * 4 + 3)] = static_cast<float>(tr);
    }
    return out;
}

std::uint32_t ray_flags(const CellMaterial& m, TextureH tex) {
    std::uint32_t f = m.semi_transparent ? static_cast<std::uint32_t>(m.blend) : 0u;
    if (tex) f |= rhi::kRayTextured;
    const WaterSurface& w = water_surface();
    if (!m.unpictured.empty() && m.unpictured == w.material) f |= rhi::kRayWater;
    return f;
}

}  // namespace

void SceneRenderer::ray_scene(const Frame& frame, rhi::RayScene* out) const {
    const Impl& im = *impl_;
    out->clear();
    if (!frame.world) return;
    constexpr std::uint32_t kStride = sizeof(GpuVertex);
    std::map<const GpuCell*, std::uint32_t> mesh_of;
    // The texture rectangles with their own material, once per page of the frame: first record and count.
    std::map<std::string, std::pair<std::uint32_t, std::uint32_t>, std::less<>> regions_of;
    auto regions = [&](const CellMaterial& cm) -> std::pair<std::uint32_t, std::uint32_t> {
        if (cm.texture.empty()) return {0, 0};
        const std::string_view stem = texture_stem(cm.texture);
        if (auto it = regions_of.find(stem); it != regions_of.end()) return it->second;
        std::pair<std::uint32_t, std::uint32_t> range{0, 0};
        if (has_surface_regions(stem)) {
            const std::vector<RegionMaterial> list = surface_regions_of(cm.texture);
            range = {static_cast<std::uint32_t>(out->regions.size()),
                     static_cast<std::uint32_t>(std::min<std::size_t>(list.size(), 255))};
            for (std::size_t i = 0; i < range.second; ++i) out->regions.push_back({list[i].rect, list[i].surface});
        }
        regions_of.emplace(std::string(stem), range);
        return range;
    };
    auto mesh_index = [&](const GpuCell& g) -> std::optional<std::uint32_t> {
        if (!g.ok || !g.mesh || !g.vertices || !g.indices) return std::nullopt;
        if (auto it = mesh_of.find(&g); it != mesh_of.end()) return it->second;
        rhi::RayMesh m;
        m.key = g.mesh.get();
        m.vertices = g.vertices;
        m.indices = g.indices;
        m.vertexCount = static_cast<std::uint32_t>(g.mesh->vertices.size());
        m.vertexStride = kStride;
        for (const CellRange& r : g.mesh->ranges) {
            const auto mi = static_cast<std::size_t>(r.material);
            if (mi >= g.mesh->materials.size()) continue;
            const TextureH tex = mi < g.textures.size() ? g.textures[mi] : TextureH{};
            const CellMaterial& cm = g.mesh->materials[mi];
            const auto [first, count] = regions(cm);
            m.geometries.push_back(
                {r.first, r.count, tex, ray_flags(cm, tex), surface_material(cm).packed(), first, count});
        }
        if (m.geometries.empty()) return std::nullopt;
        const auto index = static_cast<std::uint32_t>(out->meshes.size());
        out->meshes.push_back(std::move(m));
        mesh_of.emplace(&g, index);
        return index;
    };
    static constexpr std::array<double, 9> kIdentity{1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (const CellRef& ref : frame.cells) {
        auto it = im.cells.find(ref.stem);
        if (it == im.cells.end()) continue;
        const auto index = mesh_index(it->second);
        if (!index) continue;
        const std::array<double, 3> at{static_cast<double>(ref.cell_x) * kCellUnits, 0.0,
                                       static_cast<double>(ref.cell_z) * kCellUnits};
        out->instances.push_back({*index, ray_transform(frame.camera, kIdentity, at)});
    }
    for (const ObjectRef& o : frame.objects) {
        auto it = o.mesh ? im.models.find(o.mesh.get()) : im.models.end();
        if (it == im.models.end()) continue;
        const auto index = mesh_index(it->second);
        if (!index) continue;
        out->instances.push_back({*index, ray_transform(frame.camera, o.rot, o.trans)});
    }
    // The posed meshes of this frame, already in view space: one structure rebuilt every frame.
    if (!im.prelit_vertices || !im.prelit_indices || im.scratch_indices.empty()) return;
    rhi::RayMesh m;
    m.vertices = im.prelit_vertices;
    m.indices = im.prelit_indices;
    m.vertexCount = static_cast<std::uint32_t>(im.scratch_vertices.size());
    m.vertexStride = kStride;
    for (const PrelitDraw& d : im.prelit_draws) {
        if (d.isolated || d.count < 3) continue;
        std::uint32_t f = static_cast<std::uint32_t>(d.blend) | rhi::kRayPrelit;
        if (d.texture) f |= rhi::kRayTextured;
        if (!d.casts_shadow) f |= rhi::kRayNoShadow;
        m.geometries.push_back({d.first, d.count, d.texture, f});
    }
    if (m.geometries.empty()) return;
    out->instances.push_back({static_cast<std::uint32_t>(out->meshes.size()), {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}});
    out->meshes.push_back(std::move(m));
}

}  // namespace sh::scene
