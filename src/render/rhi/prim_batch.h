#pragma once

// Primitive batches over the RHI: the vertex layout of the frame's primitive stream, its uniform blocks, the
// fixed-function state of each blend / depth / stencil mode and the pipeline cache. The GPU renderer (render/gpu)
// hands its vertex stream and splits to PrimRenderer (RENDER.md section 2).

#include "render/rhi/rhi.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>

namespace sh::rhi {

// One vertex of the primitive stream (68 bytes, pack(1)): screen position, texture page and palette words, flat
// depth, texel, brightness mode (1 untextured, 2 modulated), dither flag, colour, half-texel offset, fog flag and
// depth mark, precise position (w 0 = affine), view-space normal, view-space position, world-geometry flag and the
// texel box the filter clamps to.
// PrimVertex::geom3d of a surface mark (draw3d::Polygon::surface_decal): the fragment shader's decal program.
inline constexpr float kGeomSurfaceDecal = 2.f;

#pragma pack(push, 1)
struct PrimVertex {
    std::int16_t x = 0, y = 0, page = 0, clut = 0;
    float z = 0.f;
    std::uint8_t u = 0, v = 0, bright = 1, dither = 0;
    std::uint8_t r = 0, g = 0, b = 0, a = 0;
    std::int8_t tcx = 0, tcy = 0, fog = 0, depthMark = 0;
    float ppx = 0.f, ppy = 0.f, ppw = 0.f;
    float nx = 0.f, ny = 0.f, nz = 0.f;
    float vsx = 0.f, vsy = 0.f, vsz = 0.f;
    float geom3d = 0.f;  // 0 flat art, 1 scene geometry, kGeomSurfaceDecal a surface mark
    std::uint8_t ulo = 0, vlo = 0, uhi = 0, vhi = 0;
};
#pragma pack(pop)
static_assert(sizeof(PrimVertex) == 68, "the vertex attributes and the shaders rely on this layout");

// Vertex page word: tpage X/Y in bits 0-4 and the tpage format (0 4-bit, 1 8-bit, 2 16-bit) in bits 5-6;
// kPageUntextured marks a flat/gouraud prim.
constexpr std::int16_t kPageUntextured = 0x80;
constexpr std::int16_t texturePageWord(int tpageX64, int tpageY256, int format) {
    return static_cast<std::int16_t>((tpageX64 & 15) | ((tpageY256 & 1) << 4) | ((format & 3) << 5));
}
// CLUT word: x / 16 in bits 0-5, y in bits 6-14.
constexpr std::int16_t paletteWord(int x, int y) { return static_cast<std::int16_t>(((y & 511) << 6) | ((x >> 4) & 63)); }

constexpr int kVramWidth = 1024;
constexpr int kVramHeight = 512;

// Semi-transparency of a split: the four hardware modes (B/2 + F/2, B + F, B - F, B + F/4) plus a constant-alpha
// mix for the frame feedback.
enum class BlendMode : std::uint8_t { None, Average, Add, Subtract, AddQuarter, ConstantAlpha };
// The mask bit as stencil: MaskWrite marks the drawn pixels, MaskTest draws only where nothing is marked.
enum class StencilMode : std::uint8_t { Off, MaskWrite, MaskTest };

struct PrimPipelineKey {
    BlendMode blend = BlendMode::None;
    bool depthTest = false;
    bool depthWrite = true;
    bool depthAlways = false;  // painter pass of precise geometry: the depth test always passes
    StencilMode stencil = StencilMode::Off;
    bool alphaToCoverage = false;  // foliage_edge 2 on a multisampled target

    std::uint32_t pack() const {
        return static_cast<std::uint32_t>(blend) | (depthTest ? 1u << 3 : 0u) | (depthWrite ? 1u << 4 : 0u) |
               (depthAlways ? 1u << 5 : 0u) | (static_cast<std::uint32_t>(stencil) << 6) |
               (alphaToCoverage ? 1u << 9 : 0u);
    }
};

// std140 VsFrame of prim_batch.vert.
struct PrimVsUniforms {
    std::array<float, 16> projection{};
    std::array<float, 4> pgxp{0.f, 65536.f, 0.f, 0.f};  // enabled, szMax, far W clamp, world far bias
    std::array<float, 4> modes{0.f, 0.f, 0.f, 0.f};     // soft fog
};
static_assert(sizeof(PrimVsUniforms) == 96);

// std140 WorldFog: the world fog ramp (render::set_world_fog), the same block in both stages.
struct FogRampUniforms {
    std::array<float, 4> params{0.f, 0.f, 0.f, 0.f};  // fog on, 128 / 2^depthShift, intensity (Q12), cull distance
    std::array<float, 128> ramp{};                     // 0..255 per entry
};
static_assert(sizeof(FogRampUniforms) == 528);

// std140 FsFrame of prim_batch.frag.
struct PrimFsUniforms {
    std::array<float, 4> fog{0.f, 0.f, 0.f, 1.1f};      // fog rgb, strength
    std::array<float, 4> modes{0.f, 0.f, 0.f, 1.f};     // fog to black, soft fog, bilinear gate, aniso taps
    std::array<float, 4> dither{0.f, 8.f, 0.f, 0.f};    // dither force, cell, deband, target height
    std::array<float, 4> flMode{0.f, 1.f, 0.f, 4096.f}; // on, style, soft highlights, range
    std::array<float, 4> flPos{0.f, 0.f, 0.f, 0.95f};   // position, inner cos
    std::array<float, 4> flDir{0.f, 0.f, 1.f, 0.85f};   // direction, outer cos
    std::array<float, 4> flColor{1.f, 1.f, 1.f, 0.f};
    std::array<float, 4> prog{0.f, 0.f, 0.f, 0.f};      // RGBA override program, feedback mode, feedback STP, shadows
    std::array<float, 4> rgbaTex{1.f, 1.f, 0.f, 0.f};   // texel size, texture offset (native texels)
    std::array<float, 4> rgbaHalf{0.f, 0.f, 0.f, 0.f};  // half a hi-res texel, feedback origin
    std::array<float, 4> rgbaFb{0.f, 0.f, 0.f, 0.f};    // 1 / feedback capture size
    std::array<float, 16> shadowMatrix{1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f};
    std::array<float, 4> shadowP{0.f, 0.f, 0.f, 0.f};   // bias, normal offset, strength, fade distance
    std::array<float, 4> shadowQ{0.f, 0.f, 1.f, 2.f};   // map texel, light near / far
};
static_assert(sizeof(PrimFsUniforms) == 272);

// Fragment sampler slots of prim_batch.frag.
enum PrimSampler : std::uint32_t { kPrimSamplerVram = 0, kPrimSamplerRgba = 1, kPrimSamplerShadow = 2 };

// Column-major GL ortho with Z in [-1, 1].
std::array<float, 16> orthoProjection(float left, float right, float bottom, float top, float znear, float zfar);

// The fixed-function state of a pipeline key.
BlendState primBlendState(BlendMode blend);
// Blend constants of a mode: AddQuarter reads the quarter through ConstantColor; ConstantAlpha mixes by
// `feedbackDamp` (the frame feedback's damping) in every component.
std::array<float, 4> primBlendConstants(BlendMode blend, float feedbackDamp = 1.f);
DepthStencilState primDepthStencil(const PrimPipelineKey& key);
// Stencil reference of both mask modes.
constexpr std::uint8_t kPrimStencilRef = 1;
// Additive and subtractive prims fade to black in fog instead of to the fog colour.
constexpr bool fogsToBlack(BlendMode blend) {
    return blend == BlendMode::Add || blend == BlendMode::Subtract || blend == BlendMode::AddQuarter;
}

// Owns the video-memory texture, the primitive shaders, a streaming vertex buffer and the pipeline cache for one
// target format. One per device.
class PrimRenderer {
public:
    static Result<std::unique_ptr<PrimRenderer>> create(Device& device, TextureFormat colorFormat,
                                                           bool depthStencil, std::uint32_t maxVertices);
    ~PrimRenderer();
    PrimRenderer(const PrimRenderer&) = delete;
    PrimRenderer& operator=(const PrimRenderer&) = delete;

    Result<void> uploadVertices(CommandList& cmd, std::span<const PrimVertex> vertices);

    Result<PipelineH> pipeline(const PrimPipelineKey& key);

    // Binds VRAM, the vertex stream and the uniform blocks for the following draws.
    void beginDraws(RenderPass& pass, const PrimVsUniforms& vs, const PrimFsUniforms& fs);
    // One split: state from `key`, fogToBlack derived from its blend.
    Result<void> draw(RenderPass& pass, const PrimPipelineKey& key, std::uint32_t firstVertex, std::uint32_t count,
                      float feedbackDamp = 1.f);
    // One split with its own uniform blocks (in the game the projection and the filter gate change per split).
    // A block is pushed only when it differs from the last one pushed; fogToBlack comes from the key's blend.
    Result<void> drawSplit(RenderPass& pass, const PrimPipelineKey& key, const PrimVsUniforms& vs,
                           const PrimFsUniforms& fs, const FogRampUniforms& fog, std::uint32_t firstVertex,
                           std::uint32_t count, float feedbackDamp = 1.f);
    // Forget what was pushed (a new pass, or foreign GL calls that may have rebound uniform buffers).
    void resetBindings() { haveVs_ = haveFs_ = haveFog_ = false; }

    // Sample `texture` as VRAM instead of the renderer's own one (the GPU renderer's video-memory texture, which
    // it uploads and feeds back itself). Empty = the own texture.
    void useVram(TextureH texture) { externalVram_ = texture; }

    TextureH vram() const { return externalVram_ ? externalVram_ : vram_; }
    BufferH vertexBuffer() const { return vertices_; }
    // Nearest, clamped for the own VRAM; nearest with repeat wrap for an external one, which is how the GPU
    // renderer's video-memory texture is sampled.
    SamplerH vramSampler() const { return externalVram_ ? externalSampler_ : sampler_; }
    std::size_t pipelineCount() const { return pipelines_.size(); }

private:
    PrimRenderer(Device& device, TextureFormat colorFormat, bool depthStencil);
    Device& device_;
    TextureFormat colorFormat_;
    bool depthStencil_;
    TextureH vram_;
    SamplerH sampler_;
    SamplerH externalSampler_;
    ShaderH vs_, fs_;
    BufferH vertices_;
    std::uint32_t maxVertices_ = 0;
    std::unordered_map<std::uint32_t, PipelineH> pipelines_;
    TextureH externalVram_;
    PrimFsUniforms fsUniforms_{};
    PrimVsUniforms vsUniforms_{};
    FogRampUniforms fogUniforms_{};
    bool lastFogToBlack_ = false;
    bool haveFs_ = false;
    bool haveVs_ = false;
    bool haveFog_ = false;
};

}  // namespace sh::rhi
