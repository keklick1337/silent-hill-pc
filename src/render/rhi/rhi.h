#pragma once

// Render hardware interface (RENDER.md section 2): one explicit-API surface for the GL 3.3 backend, the SDL3 GPU
// backend (Vulkan, D3D12, Metal) and the native Vulkan backend. No GL or Vulkan types cross it.
//
// Conventions shared by every backend, so callers never branch on the backend:
// - Clip space is the GL one the game already produces: NDC +Y up, Z in [-1, 1]. Backends with Z in
//   [0, 1] remap it in the shader (SH_RHI_CLIP_Z01, see shaders/prim_batch.vert).
// - Viewport, scissor, texture upload and readback rectangles are top-left origin, +Y down, in pixels.
//   The GL backend flips them internally; readback rows always come back top row first.
// - Rendering happens only inside a render pass; uploads are recorded on the command list outside one.
// - Uniforms are pushed per draw into std140 blocks addressed by (stage, slot), like SDL3 GPU.

#include "formats/common/status.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace sh::rhi {

// 0 = empty. The generation in the high bits catches use of a released resource.
template <class Tag>
struct Handle {
    std::uint32_t raw = 0;
    explicit operator bool() const { return raw != 0; }
    auto operator<=>(const Handle&) const = default;
};
struct TextureTag;
struct BufferTag;
struct SamplerTag;
struct ShaderTag;
struct PipelineTag;
using TextureH = Handle<TextureTag>;
using BufferH = Handle<BufferTag>;
using SamplerH = Handle<SamplerTag>;
using ShaderH = Handle<ShaderTag>;
using PipelineH = Handle<PipelineTag>;

enum class Backend : std::uint8_t { Gl33, SdlGpu, Vulkan };

// The backends that take SPIR-V with SDL3's resource layout, Z in [0, 1] and top-left framebuffer rows.
constexpr bool spirvBackend(Backend backend) { return backend != Backend::Gl33; }

// RGBA16F (half float per channel) only where Caps::floatTargets says so: the native Vulkan backend.
enum class TextureFormat : std::uint8_t { RGBA8, RG8, R8, D24S8, RGBA16F };

enum TextureUsage : std::uint32_t {
    kUsageSampled = 1u << 0,
    kUsageColorTarget = 1u << 1,
    kUsageDepthStencilTarget = 1u << 2,
};

struct TextureDesc {
    TextureFormat format = TextureFormat::RGBA8;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t usage = kUsageSampled;
    std::string_view name;
    // Above 1: a multisampled render target (Caps::maxSamples at most). It can only be drawn into and resolved
    // into a single-sample texture of the same size and format (CommandList::resolveTexture); it is never sampled,
    // copied, blitted or read back.
    std::uint32_t samples = 1;
    // Above 1: a sampled texture with that many levels, each half the one before (rounded down, at least 1),
    // filled by CommandList::uploadTextureLevel. Shaders read them with texelFetch at an explicit level.
    std::uint32_t mipLevels = 1;
};

enum class BufferUsage : std::uint8_t { Vertex, Index };

struct BufferDesc {
    BufferUsage usage = BufferUsage::Vertex;
    std::uint32_t size = 0;
    std::string_view name;
    // Also read by shaders as a storage buffer (RenderPass::bindStorageBuffer); only where Caps::storageBuffers.
    bool shaderRead = false;
};

enum class Filter : std::uint8_t { Nearest, Linear };
enum class AddressMode : std::uint8_t { ClampToEdge, Repeat, MirroredRepeat };

struct SamplerDesc {
    Filter minFilter = Filter::Nearest;
    Filter magFilter = Filter::Nearest;
    AddressMode addressU = AddressMode::ClampToEdge;
    AddressMode addressV = AddressMode::ClampToEdge;
};

// The stages after Fragment exist where Caps::tessellation / Caps::meshShaders say so. They take the vertex stage's
// resources: bindSampler and pushUniforms with ShaderStage::Vertex reach every stage before rasterisation.
enum class ShaderStage : std::uint8_t { Vertex, Fragment, TessControl, TessEval, Task, Mesh };

// One shader in both forms built from the same GLSL 450 file: the text for the GL backend (which swaps the
// #version line and leaves SH_RHI_VULKAN undefined) and the SPIR-V that glslc made from it for SDL3 GPU.
// SPIR-V resource layout is SDL3's: vertex samplers set 0 / uniforms set 1, fragment samplers set 2 /
// uniforms set 3, binding = slot. GL 3.3 has no binding qualifiers, so the names map slots to GL objects.
struct ShaderDesc {
    ShaderStage stage = ShaderStage::Vertex;
    std::string_view glsl;
    std::span<const std::uint32_t> spirv;
    std::string_view entryPoint = "main";
    std::span<const std::string_view> samplerNames;       // index = sampler slot
    std::span<const std::string_view> uniformBlockNames;  // index = uniform slot
    std::string_view name;
    // Read-only storage buffers of the stages before rasterisation (set 4, binding = slot; Caps::storageBuffers).
    std::uint32_t storageBuffers = 0;
};

// Integer formats reach the shader as ivec/uvec (the GL backend uses glVertexAttribIPointer); Vulkan has no
// "scaled" formats in SDL3, so the shader converts.
enum class VertexFormat : std::uint8_t { Float, Float2, Float3, Float4, Short4, UByte4, UByte4Norm, Byte4 };

struct VertexAttribute {
    std::uint32_t location = 0;
    VertexFormat format = VertexFormat::Float4;
    std::uint32_t offset = 0;
};

struct VertexLayout {
    std::uint32_t stride = 0;
    std::span<const VertexAttribute> attributes;  // read during createPipeline only
};

enum class BlendFactor : std::uint8_t {
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    DstColor,
    OneMinusDstColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
    ConstantColor,
    OneMinusConstantColor,
};
enum class BlendOp : std::uint8_t { Add, Subtract, ReverseSubtract, Min, Max };

enum ColorWrite : std::uint8_t { kWriteR = 1, kWriteG = 2, kWriteB = 4, kWriteA = 8, kWriteAll = 15 };

struct BlendState {
    bool enable = false;
    BlendFactor srcColor = BlendFactor::One;
    BlendFactor dstColor = BlendFactor::Zero;
    BlendOp colorOp = BlendOp::Add;
    BlendFactor srcAlpha = BlendFactor::One;
    BlendFactor dstAlpha = BlendFactor::Zero;
    BlendOp alphaOp = BlendOp::Add;
    std::uint8_t writeMask = kWriteAll;
    // The fragment's alpha becomes the sample coverage on a multisampled target; ignored on a single-sample one.
    bool alphaToCoverage = false;
};

enum class CompareOp : std::uint8_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
enum class StencilOp : std::uint8_t { Keep, Zero, Replace, IncrementClamp, DecrementClamp, Invert, IncrementWrap, DecrementWrap };

struct StencilFace {
    StencilOp fail = StencilOp::Keep;
    StencilOp depthFail = StencilOp::Keep;
    StencilOp pass = StencilOp::Keep;
    CompareOp compare = CompareOp::Always;
};

struct DepthStencilState {
    bool depthTest = false;
    bool depthWrite = false;
    CompareOp depthCompare = CompareOp::LessEqual;
    bool stencilTest = false;
    StencilFace stencil;  // both faces
    std::uint8_t stencilReadMask = 0xFF;
    std::uint8_t stencilWriteMask = 0xFF;
};

enum class CullMode : std::uint8_t { None, Front, Back };
enum class FrontFace : std::uint8_t { CounterClockwise, Clockwise };
enum class PrimitiveType : std::uint8_t { TriangleList, TriangleStrip, LineList };

// Everything a draw's fixed-function state needs, baked like SDL_GPUGraphicsPipeline. Callers cache these by
// key (PrimRenderer in prim_batch.h); the GL backend applies only the state that differs from the last bind.
struct PipelineDesc {
    ShaderH vertexShader;
    ShaderH fragmentShader;
    VertexLayout vertexLayout;
    PrimitiveType primitive = PrimitiveType::TriangleList;
    BlendState blend;
    DepthStencilState depthStencil;
    CullMode cull = CullMode::None;
    FrontFace frontFace = FrontFace::CounterClockwise;
    // Triangles drawn as their edges (the editor's wireframe view). GL backends without a polygon mode (GLES)
    // draw them filled.
    bool wireframe = false;
    TextureFormat colorFormat = TextureFormat::RGBA8;
    bool hasDepthStencil = false;
    TextureFormat depthStencilFormat = TextureFormat::D24S8;
    std::string_view name;
    // Tessellation (Caps::tessellation): both shaders set make the pipeline draw patches of `patchControlPoints`
    // vertices instead of `primitive`.
    ShaderH tessControlShader;
    ShaderH tessEvalShader;
    std::uint32_t patchControlPoints = 3;
    // Mesh shading (Caps::meshShaders): a mesh shader replaces the vertex shader, the vertex layout and `primitive`;
    // the task shader is optional. Drawn with RenderPass::drawMeshTasks.
    ShaderH taskShader;
    ShaderH meshShader;
};

enum class LoadOp : std::uint8_t { Load, Clear, DontCare };
enum class StoreOp : std::uint8_t { Store, DontCare };

struct ColorTarget {
    TextureH texture;
    LoadOp load = LoadOp::Clear;
    StoreOp store = StoreOp::Store;
    std::array<float, 4> clear{0.f, 0.f, 0.f, 1.f};
};

struct DepthStencilTarget {
    TextureH texture;  // empty = no depth/stencil attachment
    LoadOp load = LoadOp::Clear;
    StoreOp store = StoreOp::DontCare;
    float clearDepth = 1.f;
    std::uint8_t clearStencil = 0;
};

struct RenderPassDesc {
    ColorTarget color;
    DepthStencilTarget depthStencil;
    // What the GPU timing of the profiler calls the pass ("scene", "post"); unnamed passes count as "other".
    std::string_view name;
};

struct Rect {
    std::int32_t x = 0, y = 0, w = 0, h = 0;
};

struct Viewport {
    float x = 0, y = 0, w = 0, h = 0;
    float minDepth = 0.f, maxDepth = 1.f;
};

enum class IndexType : std::uint8_t { U16, U32 };

struct Caps {
    std::string driver;         // "opengl 3.3 (llvmpipe ...)", "vulkan", ...
    std::string adapter;        // renderer / device name when known
    bool clipZeroToOne = false; // backend NDC depth range is [0, 1]: shaders built with SH_RHI_CLIP_Z01
    bool fragCoordTopLeft = false; // gl_FragCoord origin top-left: the primitive shader flips Y for the dither phase
    std::uint32_t maxTextureSize = 0; // largest side createTexture takes; 0 = the backend does not report it
    std::uint32_t maxSamples = 1;     // largest TextureDesc::samples of a colour + depth/stencil target pair
    bool floatTargets = false;        // RGBA16F textures: sampled (linear), colour target with blending, blit, readback
    bool storageBuffers = false;      // BufferDesc::shaderRead, ShaderDesc::storageBuffers
    bool tessellation = false;        // PipelineDesc::tessControlShader / tessEvalShader
    bool meshShaders = false;         // PipelineDesc::taskShader / meshShader (VK_EXT_mesh_shader)
    std::string meshShadersMissing;   // why meshShaders is false on a backend that has tessellation ("" otherwise)
    bool fragmentQuadOps = false;     // SPIR-V subgroup quad operations in fragment shaders (native Vulkan)
};

// Recording inside one render pass. Valid until CommandList::endRenderPass.
class RenderPass {
public:
    virtual ~RenderPass() = default;
    virtual void bindPipeline(PipelineH pipeline) = 0;
    virtual void setViewport(const Viewport& viewport) = 0;
    virtual void setScissor(const Rect& rect) = 0;  // reset to the full target at pass begin
    virtual void setBlendConstants(const std::array<float, 4>& rgba) = 0;
    virtual void setStencilReference(std::uint8_t reference) = 0;
    virtual void bindVertexBuffer(BufferH buffer, std::uint32_t offset) = 0;
    virtual void bindIndexBuffer(BufferH buffer, IndexType type, std::uint32_t offset) = 0;
    // A texture needs a sampler (both empty unbind): without one GL samples with the texture's own state, but
    // SDL3 GPU binds nothing and the slot keeps its previous texture.
    virtual void bindSampler(ShaderStage stage, std::uint32_t slot, TextureH texture, SamplerH sampler) = 0;
    virtual void pushUniforms(ShaderStage stage, std::uint32_t slot, std::span<const std::byte> data) = 0;
    virtual void draw(std::uint32_t vertexCount, std::uint32_t firstVertex) = 0;
    virtual void drawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex, std::int32_t vertexOffset) = 0;
    // Backends without the capability (Caps) leave these empty.
    virtual void bindStorageBuffer(std::uint32_t /*slot*/, BufferH /*buffer*/, std::uint32_t /*offset*/) {}
    virtual void drawMeshTasks(std::uint32_t /*taskGroups*/) {}
    // GPU time of the draws between the two, added to ProfileStats::passes under `name` (profiling only). One
    // section at a time, inside one pass.
    virtual void beginTimedSection(std::string_view /*name*/) {}
    virtual void endTimedSection() {}
};

// One frame of work. Uploads and copies go between passes (SDL3 copy passes), so the frame-feedback reads of
// the game (framebuffer stores, captures) become explicit pass breaks.
class CommandList {
public:
    virtual ~CommandList() = default;
    // `pixels` is rect.h rows of rect.w texels, tightly packed, top row first.
    virtual void uploadTexture(TextureH texture, const Rect& rect, std::span<const std::byte> pixels) = 0;
    // The same into mip level `level` of a texture made with TextureDesc::mipLevels; level 0 is uploadTexture.
    virtual void uploadTextureLevel(TextureH texture, std::uint32_t level, const Rect& rect,
                                    std::span<const std::byte> pixels) {
        if (level == 0) uploadTexture(texture, rect, pixels);
    }
    // A write at offset 0 starts the buffer's contents afresh (GL orphans, SDL3 cycles); later writes add to it.
    virtual void uploadBuffer(BufferH buffer, std::uint32_t offset, std::span<const std::byte> data) = 0;
    virtual void copyTexture(TextureH src, const Rect& srcRect, TextureH dst, std::int32_t dstX, std::int32_t dstY) = 0;
    // Scaled copy between colour textures of any colour format, the swapchain texture included (outside a pass).
    virtual void blitTexture(TextureH src, const Rect& srcRect, TextureH dst, const Rect& dstRect, Filter filter) = 0;
    // The average of every pixel's samples of a multisampled colour target into a single-sample texture of the same
    // size and format (outside a pass).
    virtual void resolveTexture(TextureH multisampled, TextureH dst) = 0;
    virtual RenderPass* beginRenderPass(const RenderPassDesc& desc) = 0;
    virtual void endRenderPass() = 0;
    // The window's back buffer for this frame, or empty (no window, minimised). Usable as a ColorTarget;
    // submit() presents it.
    virtual TextureH acquireSwapchainTexture(std::uint32_t* width, std::uint32_t* height) = 0;
};

// What the profiler overlay reads, accumulated since the previous takeProfileStats(). A negative value means
// the backend or the driver does not report it.
struct ProfileStats {
    std::uint32_t drawCalls = 0;
    std::uint64_t triangles = 0;
    double gpuMs = -1.0;            // GPU time of the command lists that finished since the last call
    std::int64_t vramUsedKb = -1;   // whole adapter, not this process
    std::int64_t vramTotalKb = -1;
    std::int64_t vramFreeKb = -1;
    // Video memory of this process: the driver's figure where it reports one (appVramFromDriver), otherwise the
    // backend's count of its own textures and buffers. Negative when neither is known.
    std::int64_t appVramKb = -1;
    bool appVramFromDriver = false;
    // GPU time per named render pass (RenderPassDesc::name) of the same command lists as gpuMs, where the backend
    // measures it; copies and blits between passes count as "transfer".
    struct PassTime {
        std::array<char, 24> name{};
        double ms = 0.0;
    };
    static constexpr std::size_t kMaxPassTimes = 12;
    std::array<PassTime, kMaxPassTimes> passes{};
    std::uint32_t passCount = 0;
    // Causes of uneven frames, where the backend counts them (negative: not counted): input triangles drawn as
    // tessellation patches, graphics pipelines compiled on first use and the CPU time that took, texel bytes
    // uploaded to textures.
    std::int64_t patchTriangles = -1;
    std::int32_t pipelinesBuilt = -1;
    double pipelineBuildMs = -1.0;
    std::int64_t textureUploadBytes = -1;
    // What the command lists since the last call did, where the backend counts it (native Vulkan; zero elsewhere).
    // On a tile-based GPU (phones) every render pass that starts or ends loads or stores its attachments through
    // memory, so passes, loads and stores are what a frame's structure costs there: `passes` were begun on the GPU,
    // `merged` continued the pass before them on the same attachments instead (a clear inside it is `inPassClears`),
    // `loads` / `stores` are attachments read from / written to memory at a pass's start / end and `attachmentMB`
    // their bytes. `transfers` are breaks between passes for copies, blits and uploads.
    struct Counts {
        std::uint32_t frames = 0;
        std::uint32_t passes = 0, merged = 0, inPassClears = 0, loads = 0, stores = 0;
        std::uint32_t transfers = 0, copies = 0, blits = 0, textureUploads = 0, bufferUploads = 0, hoisted = 0;
        std::uint32_t pipelineBinds = 0, descriptorSets = 0, barriers = 0;
        std::uint64_t uploadBytes = 0;
        double attachmentMB = 0.0;
    } counts;
};

// The pipelines the backend builds ahead at start (Device::pipelineWarmup).
struct PipelineWarmup {
    std::uint32_t total = 0;  // queued from the manifest of earlier runs
    std::uint32_t done = 0;
    // The cache on disk did not match (first start after an update or a driver change): the builds take long enough
    // for a progress screen.
    bool fresh = false;
};

class Device {
public:
    virtual ~Device() = default;
    virtual Backend backend() const = 0;
    virtual const Caps& caps() const = 0;

    virtual Result<TextureH> createTexture(const TextureDesc& desc) = 0;
    virtual Result<BufferH> createBuffer(const BufferDesc& desc) = 0;
    virtual Result<SamplerH> createSampler(const SamplerDesc& desc) = 0;
    // A compile error is data, not an abort: the engine and a mod get the same path (RENDER.md section 2).
    virtual Result<ShaderH> createShader(const ShaderDesc& desc) = 0;
    virtual Result<PipelineH> createPipeline(const PipelineDesc& desc) = 0;
    virtual void destroy(TextureH handle) = 0;
    virtual void destroy(BufferH handle) = 0;
    virtual void destroy(SamplerH handle) = 0;
    virtual void destroy(ShaderH handle) = 0;
    virtual void destroy(PipelineH handle) = 0;

    // One open command list at a time; submit() ends it (and presents an acquired swapchain texture).
    virtual CommandList* begin() = 0;
    virtual Result<void> submit() = 0;
    // Synchronous: waits for the GPU. RGBA8 targets (and RGBA16F where Caps::floatTargets, clamped to 8 bits);
    // rows top row first.
    virtual Result<void> readback(TextureH texture, const Rect& rect, std::span<std::byte> rgba8) = 0;
    virtual void waitIdle() = 0;

    // GPU timing costs a query per command list, so it runs only while profiling is on.
    virtual void setProfiling(bool /*on*/) {}
    // Per-pass times (ProfileStats::passes) while profiling: off leaves one GPU total per command list, without the
    // timestamps between passes that can serialise the GPU.
    virtual void setPassTiming(bool /*on*/) {}
    virtual ProfileStats takeProfileStats() { return {}; }
    // The commands of the next `frames` command lists to the log, one line per pass, merge, clear and transfer
    // (profiling; backends without it ignore the call).
    virtual void traceFrames(int /*frames*/) {}

    // Pipelines built before their first use (RENDER.md section 2.2d), where the backend does it: a pipeline the
    // caller will create later is built in the background, and createPipeline then takes it as it is.
    virtual void preparePipeline(const PipelineDesc& /*desc*/) {}
    virtual PipelineWarmup pipelineWarmup() const { return {}; }
};

// Text name of a backend, as the `renderer` config key spells it.
constexpr std::string_view backendName(Backend backend) {
    switch (backend) {
        case Backend::Gl33: return "gl";
        case Backend::SdlGpu: return "sdlgpu";
        case Backend::Vulkan: return "vulkan";
    }
    return "gl";
}

}  // namespace sh::rhi
