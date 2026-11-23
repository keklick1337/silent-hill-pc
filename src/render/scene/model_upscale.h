#pragma once

// Model upscaling (RENDER.md sections 7.11 and 13): characters, enemies, world objects and
// the curved surfaces of the map drawn as PN triangles (curved point-normal triangles) cut finer the larger they are
// on screen, through the tessellation stages or through task and mesh shaders (the map always through tessellation).
// Native Vulkan only. Off draws exactly the plain path.
//
// The patch normal of a vertex is the mean of the model's own normals at its welded position (all vertices that are
// the same point of the model), so every triangle around that point bends the same way and patches meet without
// cracks. Where those normals differ the model has a hard edge there (`crease`); an edge between two such points
// stays straight.

#include "render/rhi/rhi.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace sh::scene {

// The model_upscaling key: 0 off, 1 tessellation, 2 mesh shaders.
enum class UpscaleMode : int { Off = 0, Tessellation = 1, MeshShaders = 2 };

inline constexpr int kUpscaleMaxLevel = 16;               // the mesh stage's output limit: 16^2 triangles per patch
inline constexpr float kUpscaleTargetPixels = 8.0f;       // the length of a cut edge on screen the level aims for
inline constexpr float kUpscaleFoldCos = 0.766f;          // cos 40 degrees: the fold above which a position is a crease
inline constexpr std::uint64_t kUpscaleBudget = 1500000;  // triangles one frame may make at most
inline constexpr std::uint64_t kUpscaleMapBudget = 1500000;  // the same for the map's curved triangles

// The mode the setting gets on a device: mesh shaders fall back to tessellation, tessellation to off.
UpscaleMode upscale_mode_for(int setting, const rhi::Caps& caps);
// Why `mode` is not available on the device ("" when it is). An English sentence for the log and the console.
std::string upscale_missing(UpscaleMode mode, rhi::Backend backend, const rhi::Caps& caps);

// The mode the renderer draws with this frame: the pose code prepares patch normals only while it is not Off.
UpscaleMode upscale_active();
void set_upscale_active(UpscaleMode mode);

struct PnWeld {
    std::vector<std::uint32_t> rep;    // per vertex: the first vertex at its welded position
    std::vector<std::uint8_t> crease;  // per vertex: its position's normals split (a hard edge passes there)
};

// Welds vertices with the same key (the bytes that place a vertex: its position, or its skin influences); `normals`
// (unit, per vertex) decide the creases.
PnWeld pn_weld(std::span<const std::string> keys, std::span<const std::array<float, 3>> normals);

// Per vertex: xyz the mean normal of its welded position, w = 1 on a crease (scene_pn.glsl). A position is a crease
// too where a triangle around it (`xyz`: 3 floats per vertex, `indices`: triangle list) turns more than
// 40 degrees (kUpscaleFoldCos) from that mean: a box or a window frame whose file shares one normal across a
// right-angled corner keeps its edges straight instead of bulging. `geometric` takes the mean of the faces around a
// position instead of the file's normals (world objects: their normals are set for the PS1 light, not for the shape).
void pn_normals(const PnWeld& weld, std::span<const std::array<float, 3>> normals, std::span<const float> xyz,
                std::span<const std::uint32_t> indices, std::vector<std::array<float, 4>>& out, bool geometric = false);

// Map surfaces (area cells): the control points of every edge are made here, once per edge, and both faces of the
// edge take them, so they always meet. A corner's normal is the mean of the faces that meet at its position across
// smooth edges (opposite windings, folds under 30 degrees, or under 75 where both faces are long strips along the
// edge: a barrel, a hexagonal pillar), weighed by angle and squared area, so a large flat face keeps its plane. A
// smooth edge bends by those normals (PN triangles); a hard edge runs along both faces' tangent planes, so a pillar's
// rim rounds while its cap stays flat; an open edge, an edge of three faces or one next to a pinned face stays
// straight. Only triangles with a bent edge are drawn as patches; a flat face stays flat.
inline constexpr float kUpscaleSmoothCos = 0.866f;  // cos 30 degrees: terrain and finely cut surfaces
inline constexpr std::uint8_t kPnPinned = 1;        // per triangle: drawn flat, every edge of it straight
inline constexpr std::uint8_t kPnOverlay = 2;       // per triangle: a decal; a bending patch under it is pinned

struct PnSurface {
    std::vector<std::uint8_t> curved;  // per triangle: drawn as a patch
    // Two per corner of every triangle: the offset (model units) from the corner to the control point next to it on
    // its edge to the next corner, w 2 when that edge is straight; then the same on its edge to the previous corner.
    std::vector<std::array<float, 4>> corners;
    std::uint32_t curved_count = 0;
};

// `xyz`: 3 floats per vertex, `indices`: triangle list, `flags`: per triangle (kPnPinned, kPnOverlay; may be empty).
PnSurface pn_surface(std::span<const float> xyz, std::span<const std::uint32_t> indices,
                     std::span<const std::uint8_t> flags);

// What sets one model's level: its triangles' mean edge (view units), its nearest view depth and its triangle count.
struct PnModel {
    double mean_edge = 0;
    double nearest_z = 0;
    std::uint32_t triangles = 0;
};

// The level of every model: the mean edge seen at the nearest depth (h: the projection distance, px_per_unit: window
// pixels per screen unit) cut to kUpscaleTargetPixels pieces, 1..kUpscaleMaxLevel; then, while the frame would make
// more than `budget` triangles, the largest level steps down (the first such model first), so the cost stays
// bounded.
std::vector<int> pn_levels(std::span<const PnModel> models, double h, double px_per_unit,
                           std::uint64_t budget = kUpscaleBudget);

// The GPU side: shaders and pipelines of both paths (made on first use), and the draw.
class ModelUpscaler {
public:
    explicit ModelUpscaler(rhi::Device& device);
    ~ModelUpscaler();
    ModelUpscaler(const ModelUpscaler&) = delete;
    ModelUpscaler& operator=(const ModelUpscaler&) = delete;

    // The mode of this frame for the `model_upscaling` setting; makes the shaders the first time. Off when they fail.
    UpscaleMode begin_frame(int setting);
    UpscaleMode mode() const { return mode_; }

    // `base` (a scene pipeline) with the current mode's stages, cached by `slot`; empty when the mode is Off.
    // `tessellation`: the tessellation stages even in the mesh shader mode (map surfaces: per-edge levels).
    rhi::PipelineH pipeline(int slot, const rhi::PipelineDesc& base, bool tessellation = false);

    struct Draw {
        rhi::BufferH patch_normals;  // vec4 per vertex (pn_normals)
        rhi::BufferH vertices;       // the scene vertices, read by the mesh path
        rhi::BufferH indices;
        std::uint32_t first = 0;  // first index
        std::uint32_t count = 0;  // indices
        int level = 1;
        // A map surface (pn_surface): the tessellation path always, every edge cut by its own length on screen up
        // to `level`; `px_scale` = window pixels per view unit at depth 1.
        bool adaptive = false;
        float px_scale = 0;
    };
    // Draws with the pipeline the caller bound (one of pipeline()), timed under the mode's name in the profiler.
    void draw(rhi::RenderPass& pass, const Draw& d);

private:
    rhi::Device& device_;
    UpscaleMode mode_ = UpscaleMode::Off;
    bool tried_tess_ = false, tried_mesh_ = false;
    rhi::ShaderH vert_, tesc_, tese_, task_, mesh_;
    struct Cached {
        int slot = -1;
        UpscaleMode mode = UpscaleMode::Off;
        rhi::PipelineH pipeline;
    };
    std::vector<Cached> pipelines_;
};

}  // namespace sh::scene
