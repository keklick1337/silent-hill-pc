#include "render/rhi/rhi_sdlgpu.h"

#include "render/rhi/rhi_pool.h"
#include "render/rhi/shader_overrides.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace sh::rhi {

namespace {

std::string sdlError(const char* what) { return std::string("sdlgpu: ") + what + ": " + SDL_GetError(); }

SDL_GPUVertexElementFormat sdlVertexFormat(VertexFormat f) {
    switch (f) {
        case VertexFormat::Float: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
        case VertexFormat::Float2: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        case VertexFormat::Float3: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        case VertexFormat::Float4: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        case VertexFormat::Short4: return SDL_GPU_VERTEXELEMENTFORMAT_SHORT4;
        case VertexFormat::UByte4: return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4;
        case VertexFormat::UByte4Norm: return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
        case VertexFormat::Byte4: return SDL_GPU_VERTEXELEMENTFORMAT_BYTE4;
    }
    return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
}

SDL_GPUBlendFactor sdlBlendFactor(BlendFactor f) {
    switch (f) {
        case BlendFactor::Zero: return SDL_GPU_BLENDFACTOR_ZERO;
        case BlendFactor::One: return SDL_GPU_BLENDFACTOR_ONE;
        case BlendFactor::SrcColor: return SDL_GPU_BLENDFACTOR_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::DstColor: return SDL_GPU_BLENDFACTOR_DST_COLOR;
        case BlendFactor::OneMinusDstColor: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
        case BlendFactor::SrcAlpha: return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha: return SDL_GPU_BLENDFACTOR_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
        case BlendFactor::ConstantColor: return SDL_GPU_BLENDFACTOR_CONSTANT_COLOR;
        case BlendFactor::OneMinusConstantColor: return SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR;
    }
    return SDL_GPU_BLENDFACTOR_ONE;
}

SDL_GPUBlendOp sdlBlendOp(BlendOp op) {
    switch (op) {
        case BlendOp::Add: return SDL_GPU_BLENDOP_ADD;
        case BlendOp::Subtract: return SDL_GPU_BLENDOP_SUBTRACT;
        case BlendOp::ReverseSubtract: return SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
        case BlendOp::Min: return SDL_GPU_BLENDOP_MIN;
        case BlendOp::Max: return SDL_GPU_BLENDOP_MAX;
    }
    return SDL_GPU_BLENDOP_ADD;
}

SDL_GPUCompareOp sdlCompare(CompareOp op) {
    switch (op) {
        case CompareOp::Never: return SDL_GPU_COMPAREOP_NEVER;
        case CompareOp::Less: return SDL_GPU_COMPAREOP_LESS;
        case CompareOp::Equal: return SDL_GPU_COMPAREOP_EQUAL;
        case CompareOp::LessEqual: return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        case CompareOp::Greater: return SDL_GPU_COMPAREOP_GREATER;
        case CompareOp::NotEqual: return SDL_GPU_COMPAREOP_NOT_EQUAL;
        case CompareOp::GreaterEqual: return SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
        case CompareOp::Always: return SDL_GPU_COMPAREOP_ALWAYS;
    }
    return SDL_GPU_COMPAREOP_ALWAYS;
}

SDL_GPUStencilOp sdlStencilOp(StencilOp op) {
    switch (op) {
        case StencilOp::Keep: return SDL_GPU_STENCILOP_KEEP;
        case StencilOp::Zero: return SDL_GPU_STENCILOP_ZERO;
        case StencilOp::Replace: return SDL_GPU_STENCILOP_REPLACE;
        case StencilOp::IncrementClamp: return SDL_GPU_STENCILOP_INCREMENT_AND_CLAMP;
        case StencilOp::DecrementClamp: return SDL_GPU_STENCILOP_DECREMENT_AND_CLAMP;
        case StencilOp::Invert: return SDL_GPU_STENCILOP_INVERT;
        case StencilOp::IncrementWrap: return SDL_GPU_STENCILOP_INCREMENT_AND_WRAP;
        case StencilOp::DecrementWrap: return SDL_GPU_STENCILOP_DECREMENT_AND_WRAP;
    }
    return SDL_GPU_STENCILOP_KEEP;
}

SDL_GPULoadOp sdlLoad(LoadOp op) {
    switch (op) {
        case LoadOp::Load: return SDL_GPU_LOADOP_LOAD;
        case LoadOp::Clear: return SDL_GPU_LOADOP_CLEAR;
        case LoadOp::DontCare: return SDL_GPU_LOADOP_DONT_CARE;
    }
    return SDL_GPU_LOADOP_LOAD;
}

SDL_GPUStoreOp sdlStore(StoreOp op) { return op == StoreOp::Store ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE; }

std::uint32_t bytesPerTexel(TextureFormat f) {
    switch (f) {
        case TextureFormat::RGBA8: return 4;
        case TextureFormat::RG8: return 2;
        case TextureFormat::R8: return 1;
        case TextureFormat::D24S8: return 4;
        case TextureFormat::RGBA16F:
            return 8;
    }
    return 4;
}

struct SdlTexture {
    SDL_GPUTexture* texture = nullptr;
    TextureDesc desc;
    bool owned = true;  // false for the swapchain texture of the current frame
    std::uint64_t bytes = 0;
};
struct SdlBuffer {
    SDL_GPUBuffer* buffer = nullptr;
    BufferDesc desc;
};
struct SdlSampler {
    SDL_GPUSampler* sampler = nullptr;
};
struct SdlShader {
    SDL_GPUShader* shader = nullptr;
};
struct SdlPipeline {
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
};

class SdlGpuDevice;

class SdlRenderPass final : public RenderPass {
public:
    explicit SdlRenderPass(SdlGpuDevice& device) : dev_(device) {}
    void bindPipeline(PipelineH pipeline) override;
    void setViewport(const Viewport& viewport) override;
    void setScissor(const Rect& rect) override;
    void setBlendConstants(const std::array<float, 4>& rgba) override;
    void setStencilReference(std::uint8_t reference) override;
    void bindVertexBuffer(BufferH buffer, std::uint32_t offset) override;
    void bindIndexBuffer(BufferH buffer, IndexType type, std::uint32_t offset) override;
    void bindSampler(ShaderStage stage, std::uint32_t slot, TextureH texture, SamplerH sampler) override;
    void pushUniforms(ShaderStage stage, std::uint32_t slot, std::span<const std::byte> data) override;
    void draw(std::uint32_t vertexCount, std::uint32_t firstVertex) override;
    void drawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex, std::int32_t vertexOffset) override;

    SDL_GPURenderPass* pass = nullptr;

private:
    SdlGpuDevice& dev_;
};

class SdlCommandList final : public CommandList {
public:
    explicit SdlCommandList(SdlGpuDevice& device) : dev_(device) {}
    void uploadTexture(TextureH texture, const Rect& rect, std::span<const std::byte> pixels) override;
    void uploadTextureLevel(TextureH texture, std::uint32_t level, const Rect& rect,
                            std::span<const std::byte> pixels) override;
    void uploadBuffer(BufferH buffer, std::uint32_t offset, std::span<const std::byte> data) override;
    void copyTexture(TextureH src, const Rect& srcRect, TextureH dst, std::int32_t dstX, std::int32_t dstY) override;
    void blitTexture(TextureH src, const Rect& srcRect, TextureH dst, const Rect& dstRect, Filter filter) override;
    void resolveTexture(TextureH multisampled, TextureH dst) override;
    RenderPass* beginRenderPass(const RenderPassDesc& desc) override;
    void endRenderPass() override;
    TextureH acquireSwapchainTexture(std::uint32_t* width, std::uint32_t* height) override;

    SDL_GPUCopyPass* copyPass();
    void endCopyPass();

private:
    SdlGpuDevice& dev_;
};

class SdlGpuDevice final : public Device {
public:
    explicit SdlGpuDevice(const SdlGpuDeviceDesc& desc) : desc_(desc), cmd_(*this), pass_(*this) {}
    ~SdlGpuDevice() override;

    Result<void> init();

    Backend backend() const override { return Backend::SdlGpu; }
    const Caps& caps() const override { return caps_; }
    Result<TextureH> createTexture(const TextureDesc& desc) override;
    Result<BufferH> createBuffer(const BufferDesc& desc) override;
    Result<SamplerH> createSampler(const SamplerDesc& desc) override;
    Result<ShaderH> createShader(const ShaderDesc& desc) override;
    Result<PipelineH> createPipeline(const PipelineDesc& desc) override;
    void destroy(TextureH handle) override;
    void destroy(BufferH handle) override;
    void destroy(SamplerH handle) override;
    void destroy(ShaderH handle) override;
    void destroy(PipelineH handle) override;
    CommandList* begin() override;
    Result<void> submit() override;
    Result<void> readback(TextureH texture, const Rect& rect, std::span<std::byte> rgba8) override;
    void waitIdle() override { SDL_WaitForGPUIdle(device_); }
    // SDL_GPU has no timer queries and hides its Vulkan device: video memory is the count of this device's own
    // textures and buffers (the native Vulkan backend reads the driver's figure).
    ProfileStats takeProfileStats() override;
    void countDraw(std::uint32_t count) {
        ++drawCalls_;
        triangles_ += count / 3;
    }
    std::uint32_t drawCalls_ = 0;
    std::uint64_t triangles_ = 0;
    void setPresentMode(bool vsync) {
        if (!windowClaimed_) return;
        SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;
        if (!vsync) {
            if (SDL_WindowSupportsGPUPresentMode(device_, desc_.window, SDL_GPU_PRESENTMODE_IMMEDIATE))
                mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
            else if (SDL_WindowSupportsGPUPresentMode(device_, desc_.window, SDL_GPU_PRESENTMODE_MAILBOX))
                mode = SDL_GPU_PRESENTMODE_MAILBOX;
        }
        SDL_SetGPUSwapchainParameters(device_, desc_.window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode);
    }

private:
    friend class SdlRenderPass;
    friend class SdlCommandList;

    SDL_GPUTextureFormat textureFormat(TextureFormat f) const {
        switch (f) {
            case TextureFormat::RGBA8: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            case TextureFormat::RG8: return SDL_GPU_TEXTUREFORMAT_R8G8_UNORM;
            case TextureFormat::R8: return SDL_GPU_TEXTUREFORMAT_R8_UNORM;
            case TextureFormat::D24S8: return depthFormat_;
            case TextureFormat::RGBA16F:
                break;  // refused by createTexture (Caps::floatTargets)
        }
        return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    }

    SdlGpuDeviceDesc desc_;
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUTextureFormat depthFormat_ = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    Caps caps_;
    detail::Pool<TextureH, SdlTexture> textures_;
    detail::Pool<BufferH, SdlBuffer> buffers_;
    detail::Pool<SamplerH, SdlSampler> samplers_;
    detail::Pool<ShaderH, SdlShader> shaders_;
    detail::Pool<PipelineH, SdlPipeline> pipelines_;
    SDL_GPUCommandBuffer* cmdbuf_ = nullptr;
    SDL_GPUCopyPass* copyPass_ = nullptr;
    TextureH swapchain_;
    bool windowClaimed_ = false;
    std::uint64_t appVramBytes_ = 0;  // the live textures and buffers of this device, without SDL's own pools
    SdlCommandList cmd_;
    SdlRenderPass pass_;
};

SdlGpuDevice::~SdlGpuDevice() {
    if (!device_) return;
    SDL_WaitForGPUIdle(device_);
    pipelines_.forEach([&](SdlPipeline& p) { SDL_ReleaseGPUGraphicsPipeline(device_, p.pipeline); });
    shaders_.forEach([&](SdlShader& s) { SDL_ReleaseGPUShader(device_, s.shader); });
    samplers_.forEach([&](SdlSampler& s) { SDL_ReleaseGPUSampler(device_, s.sampler); });
    buffers_.forEach([&](SdlBuffer& b) { SDL_ReleaseGPUBuffer(device_, b.buffer); });
    textures_.forEach([&](SdlTexture& t) {
        if (t.owned) SDL_ReleaseGPUTexture(device_, t.texture);
    });
    if (windowClaimed_) SDL_ReleaseWindowFromGPUDevice(device_, desc_.window);
    SDL_DestroyGPUDevice(device_);
}

Result<void> SdlGpuDevice::init() {
    device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, desc_.debug, desc_.driver);
    if (!device_) return Result<void>::fail(sdlError("SDL_CreateGPUDevice"));
    caps_.driver = SDL_GetGPUDeviceDriver(device_);
#if SDL_VERSION_ATLEAST(3, 4, 0)
    if (SDL_PropertiesID props = SDL_GetGPUDeviceProperties(device_)) {
        const char* name = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_NAME_STRING, "");
        const char* driver = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING, "");
        caps_.adapter = std::string(name) + " / " + driver;
    }
#endif
    // SDL3 GPU: NDC +Y up like GL, but depth in [0, 1] and viewport / gl_FragCoord top-left.
    caps_.clipZeroToOne = true;
    caps_.fragCoordTopLeft = true;
    if (!SDL_GPUTextureSupportsFormat(device_, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT, SDL_GPU_TEXTURETYPE_2D,
                                      SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
        depthFormat_ = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    if (desc_.window) {
        if (!SDL_ClaimWindowForGPUDevice(device_, desc_.window)) return Result<void>::fail(sdlError("claim window"));
        windowClaimed_ = true;
    }
    return Result<void>::ok();
}

Result<TextureH> SdlGpuDevice::createTexture(const TextureDesc& desc) {
    // Caps::maxSamples stays 1 here: SDL3 GPU pipelines are baked per sample count, which the pipeline caches do
    // not key on yet, so the scene renders single-sampled on this backend.
    if (desc.samples > 1) return Result<TextureH>::fail("sdlgpu: multisampled targets are not supported");
    if (desc.format == TextureFormat::RGBA16F) return Result<TextureH>::fail("sdlgpu: no float targets");
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = textureFormat(desc.format);
    if (desc.usage & kUsageSampled) info.usage |= SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (desc.usage & kUsageColorTarget) info.usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    if (desc.usage & kUsageDepthStencilTarget) info.usage |= SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    info.width = desc.width;
    info.height = desc.height;
    info.layer_count_or_depth = 1;
    info.num_levels = std::max<Uint32>(desc.mipLevels, 1);
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SdlTexture t;
    t.texture = SDL_CreateGPUTexture(device_, &info);
    if (!t.texture) return Result<TextureH>::fail(sdlError("SDL_CreateGPUTexture"));
    if (!desc.name.empty()) SDL_SetGPUTextureName(device_, t.texture, std::string(desc.name).c_str());
    t.desc = desc;
    t.desc.name = {};
    t.bytes = std::uint64_t{desc.width} * desc.height * bytesPerTexel(desc.format);
    appVramBytes_ += t.bytes;
    return Result<TextureH>::ok(textures_.add(t));
}

Result<BufferH> SdlGpuDevice::createBuffer(const BufferDesc& desc) {
    SDL_GPUBufferCreateInfo info{};
    info.usage = desc.usage == BufferUsage::Index ? SDL_GPU_BUFFERUSAGE_INDEX : SDL_GPU_BUFFERUSAGE_VERTEX;
    info.size = desc.size;
    SdlBuffer b;
    b.buffer = SDL_CreateGPUBuffer(device_, &info);
    if (!b.buffer) return Result<BufferH>::fail(sdlError("SDL_CreateGPUBuffer"));
    b.desc = desc;
    b.desc.name = {};
    appVramBytes_ += desc.size;
    return Result<BufferH>::ok(buffers_.add(b));
}

Result<SamplerH> SdlGpuDevice::createSampler(const SamplerDesc& desc) {
    auto address = [](AddressMode m) {
        switch (m) {
            case AddressMode::ClampToEdge: return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            case AddressMode::Repeat: return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
            case AddressMode::MirroredRepeat: return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
        }
        return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    };
    SDL_GPUSamplerCreateInfo info{};
    info.min_filter = desc.minFilter == Filter::Linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    info.mag_filter = desc.magFilter == Filter::Linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.address_mode_u = address(desc.addressU);
    info.address_mode_v = address(desc.addressV);
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.min_lod = 0.f;
    info.max_lod = 0.f;
    SdlSampler s;
    s.sampler = SDL_CreateGPUSampler(device_, &info);
    if (!s.sampler) return Result<SamplerH>::fail(sdlError("SDL_CreateGPUSampler"));
    return Result<SamplerH>::ok(samplers_.add(s));
}

Result<ShaderH> SdlGpuDevice::createShader(const ShaderDesc& desc) {
    if (desc.spirv.empty()) return Result<ShaderH>::fail("sdlgpu: shader " + std::string(desc.name) + " has no SPIR-V");
    if (desc.stage != ShaderStage::Vertex && desc.stage != ShaderStage::Fragment)
        return Result<ShaderH>::fail("sdlgpu: shader " + std::string(desc.name) + ": no such stage on this backend");
    const std::string entry(desc.entryPoint);
    SDL_GPUShaderCreateInfo info{};
    info.code_size = desc.spirv.size_bytes();
    info.code = reinterpret_cast<const Uint8*>(desc.spirv.data());
    info.entrypoint = entry.c_str();
    info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    info.stage = desc.stage == ShaderStage::Vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers = static_cast<Uint32>(desc.samplerNames.size());
    info.num_uniform_buffers = static_cast<Uint32>(desc.uniformBlockNames.size());
    SdlShader s;
    // A mod's SPIR-V (shaders/<file>.spv): whole 32-bit words starting with the SPIR-V magic, else the built-in.
    if (const std::optional<std::string> mod = shader_override(desc.name, ".spv"); mod) {
        const std::string where = "shaders/" + shader_file_name(desc.name) + ".spv";
        std::uint32_t magic = 0;
        if (mod->size() >= 20 && mod->size() % 4 == 0) std::memcpy(&magic, mod->data(), sizeof magic);
        if (magic != 0x07230203u) {
            shader_log(1, where + " is not SPIR-V, the built-in shader is used");
        } else {
            SDL_GPUShaderCreateInfo own = info;
            own.code_size = mod->size();
            own.code = reinterpret_cast<const Uint8*>(mod->data());
            s.shader = SDL_CreateGPUShader(device_, &own);
            if (s.shader) {
                shader_log(0, "shader " + std::string(desc.name) + ": the mod's " + where);
            } else {
                shader_log(1, where + ": " + sdlError("SDL_CreateGPUShader") + ", the built-in shader is used");
            }
        }
    }
    if (!s.shader) s.shader = SDL_CreateGPUShader(device_, &info);
    if (!s.shader) return Result<ShaderH>::fail(sdlError(("shader " + std::string(desc.name)).c_str()));
    return Result<ShaderH>::ok(shaders_.add(s));
}

Result<PipelineH> SdlGpuDevice::createPipeline(const PipelineDesc& desc) {
    SdlShader* vs = shaders_.get(desc.vertexShader);
    SdlShader* fs = shaders_.get(desc.fragmentShader);
    if (!vs || !fs) return Result<PipelineH>::fail("sdlgpu: pipeline with a released shader");

    SDL_GPUVertexBufferDescription vb{};
    vb.slot = 0;
    vb.pitch = desc.vertexLayout.stride;
    vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    std::vector<SDL_GPUVertexAttribute> attrs;
    for (const VertexAttribute& a : desc.vertexLayout.attributes)
        attrs.push_back({a.location, 0, sdlVertexFormat(a.format), a.offset});

    SDL_GPUColorTargetDescription color{};
    color.format = textureFormat(desc.colorFormat);
    const BlendState& b = desc.blend;
    color.blend_state.enable_blend = b.enable;
    color.blend_state.src_color_blendfactor = sdlBlendFactor(b.srcColor);
    color.blend_state.dst_color_blendfactor = sdlBlendFactor(b.dstColor);
    color.blend_state.color_blend_op = sdlBlendOp(b.colorOp);
    color.blend_state.src_alpha_blendfactor = sdlBlendFactor(b.srcAlpha);
    color.blend_state.dst_alpha_blendfactor = sdlBlendFactor(b.dstAlpha);
    color.blend_state.alpha_blend_op = sdlBlendOp(b.alphaOp);
    color.blend_state.color_write_mask = b.writeMask;
    color.blend_state.enable_color_write_mask = true;

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vs->shader;
    info.fragment_shader = fs->shader;
    info.vertex_input_state.vertex_buffer_descriptions = &vb;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attrs.data();
    info.vertex_input_state.num_vertex_attributes = static_cast<Uint32>(attrs.size());
    switch (desc.primitive) {
        case PrimitiveType::TriangleList: info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST; break;
        case PrimitiveType::TriangleStrip: info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP; break;
        case PrimitiveType::LineList: info.primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST; break;
    }
    info.rasterizer_state.fill_mode = desc.wireframe ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = desc.cull == CullMode::None    ? SDL_GPU_CULLMODE_NONE
                                      : desc.cull == CullMode::Front ? SDL_GPU_CULLMODE_FRONT
                                                                     : SDL_GPU_CULLMODE_BACK;
    info.rasterizer_state.front_face = desc.frontFace == FrontFace::Clockwise ? SDL_GPU_FRONTFACE_CLOCKWISE
                                                                             : SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.rasterizer_state.enable_depth_clip = true;  // GL clips unless GL_DEPTH_CLAMP, which the game never sets
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    const DepthStencilState& s = desc.depthStencil;
    info.depth_stencil_state.enable_depth_test = s.depthTest;
    info.depth_stencil_state.enable_depth_write = s.depthWrite && s.depthTest;
    info.depth_stencil_state.compare_op = sdlCompare(s.depthCompare);
    info.depth_stencil_state.enable_stencil_test = s.stencilTest;
    SDL_GPUStencilOpState face{sdlStencilOp(s.stencil.fail), sdlStencilOp(s.stencil.pass),
                               sdlStencilOp(s.stencil.depthFail), sdlCompare(s.stencil.compare)};
    info.depth_stencil_state.front_stencil_state = face;
    info.depth_stencil_state.back_stencil_state = face;
    info.depth_stencil_state.compare_mask = s.stencilReadMask;
    info.depth_stencil_state.write_mask = s.stencilWriteMask;
    info.target_info.color_target_descriptions = &color;
    info.target_info.num_color_targets = 1;
    info.target_info.has_depth_stencil_target = desc.hasDepthStencil;
    info.target_info.depth_stencil_format = textureFormat(desc.depthStencilFormat);

    SdlPipeline p;
    p.pipeline = SDL_CreateGPUGraphicsPipeline(device_, &info);
    if (!p.pipeline) return Result<PipelineH>::fail(sdlError("SDL_CreateGPUGraphicsPipeline"));
    return Result<PipelineH>::ok(pipelines_.add(p));
}

void SdlGpuDevice::destroy(TextureH handle) {
    if (auto t = textures_.remove(handle); t && t->owned) {
        SDL_ReleaseGPUTexture(device_, t->texture);
        appVramBytes_ -= t->bytes;
    }
}
void SdlGpuDevice::destroy(BufferH handle) {
    if (auto b = buffers_.remove(handle)) {
        SDL_ReleaseGPUBuffer(device_, b->buffer);
        appVramBytes_ -= b->desc.size;
    }
}

ProfileStats SdlGpuDevice::takeProfileStats() {
    ProfileStats out;
    out.drawCalls = drawCalls_;
    out.triangles = triangles_;
    drawCalls_ = 0;
    triangles_ = 0;
    out.appVramKb = static_cast<std::int64_t>(appVramBytes_ / 1024);
    return out;
}
void SdlGpuDevice::destroy(SamplerH handle) {
    if (auto s = samplers_.remove(handle)) SDL_ReleaseGPUSampler(device_, s->sampler);
}
void SdlGpuDevice::destroy(ShaderH handle) {
    if (auto s = shaders_.remove(handle)) SDL_ReleaseGPUShader(device_, s->shader);
}
void SdlGpuDevice::destroy(PipelineH handle) {
    if (auto p = pipelines_.remove(handle)) SDL_ReleaseGPUGraphicsPipeline(device_, p->pipeline);
}

CommandList* SdlGpuDevice::begin() {
    if (cmdbuf_) return nullptr;
    cmdbuf_ = SDL_AcquireGPUCommandBuffer(device_);
    return cmdbuf_ ? &cmd_ : nullptr;
}

Result<void> SdlGpuDevice::submit() {
    if (!cmdbuf_) return Result<void>::fail("sdlgpu: submit without begin");
    cmd_.endRenderPass();
    cmd_.endCopyPass();
    const bool ok = SDL_SubmitGPUCommandBuffer(cmdbuf_);
    cmdbuf_ = nullptr;
    if (swapchain_) {
        textures_.remove(swapchain_);
        swapchain_ = {};
    }
    return ok ? Result<void>::ok() : Result<void>::fail(sdlError("SDL_SubmitGPUCommandBuffer"));
}

Result<void> SdlGpuDevice::readback(TextureH texture, const Rect& rect, std::span<std::byte> rgba8) {
    if (cmdbuf_) return Result<void>::fail("sdlgpu: readback with an open command list");
    SdlTexture* t = textures_.get(texture);
    if (!t) return Result<void>::fail("sdlgpu: readback of a released texture");
    if (t->desc.format != TextureFormat::RGBA8) return Result<void>::fail("sdlgpu: readback needs RGBA8");
    const Uint32 size = static_cast<Uint32>(rect.w) * static_cast<Uint32>(rect.h) * 4;
    if (rgba8.size() < size) return Result<void>::fail("sdlgpu: readback buffer small");

    SDL_GPUTransferBufferCreateInfo tbInfo{};
    tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tbInfo.size = size;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device_, &tbInfo);
    if (!tb) return Result<void>::fail(sdlError("download buffer"));
    SDL_GPUCommandBuffer* cb = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cb);
    SDL_GPUTextureRegion src{};
    src.texture = t->texture;
    src.x = static_cast<Uint32>(rect.x);
    src.y = static_cast<Uint32>(rect.y);
    src.w = static_cast<Uint32>(rect.w);
    src.h = static_cast<Uint32>(rect.h);
    src.d = 1;
    SDL_GPUTextureTransferInfo dst{};
    dst.transfer_buffer = tb;
    dst.pixels_per_row = static_cast<Uint32>(rect.w);
    dst.rows_per_layer = static_cast<Uint32>(rect.h);
    SDL_DownloadFromGPUTexture(cp, &src, &dst);
    SDL_EndGPUCopyPass(cp);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cb);
    if (!fence) {
        SDL_ReleaseGPUTransferBuffer(device_, tb);
        return Result<void>::fail(sdlError("readback submit"));
    }
    SDL_WaitForGPUFences(device_, true, &fence, 1);
    SDL_ReleaseGPUFence(device_, fence);
    void* mapped = SDL_MapGPUTransferBuffer(device_, tb, false);
    if (mapped) {
        std::memcpy(rgba8.data(), mapped, size);
        SDL_UnmapGPUTransferBuffer(device_, tb);
    }
    SDL_ReleaseGPUTransferBuffer(device_, tb);
    return mapped ? Result<void>::ok() : Result<void>::fail(sdlError("map download buffer"));
}

// ---- command list -----------------------------------------------------------------------------------------

SDL_GPUCopyPass* SdlCommandList::copyPass() {
    endRenderPass();
    if (!dev_.copyPass_) dev_.copyPass_ = SDL_BeginGPUCopyPass(dev_.cmdbuf_);
    return dev_.copyPass_;
}

void SdlCommandList::endCopyPass() {
    if (!dev_.copyPass_) return;
    SDL_EndGPUCopyPass(dev_.copyPass_);
    dev_.copyPass_ = nullptr;
}

// Transfer buffers are released right after recording: SDL frees them once the GPU is done with them.
void SdlCommandList::uploadTexture(TextureH texture, const Rect& rect, std::span<const std::byte> pixels) {
    uploadTextureLevel(texture, 0, rect, pixels);
}

void SdlCommandList::uploadTextureLevel(TextureH texture, std::uint32_t level, const Rect& rect,
                                        std::span<const std::byte> pixels) {
    SdlTexture* t = dev_.textures_.get(texture);
    if (!t || !t->owned || rect.w <= 0 || rect.h <= 0) return;
    const Uint32 size = static_cast<Uint32>(rect.w) * static_cast<Uint32>(rect.h) * bytesPerTexel(t->desc.format);
    if (pixels.size() < size) return;
    SDL_GPUTransferBufferCreateInfo tbInfo{};
    tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbInfo.size = size;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev_.device_, &tbInfo);
    if (!tb) return;
    if (void* mapped = SDL_MapGPUTransferBuffer(dev_.device_, tb, false)) {
        std::memcpy(mapped, pixels.data(), size);
        SDL_UnmapGPUTransferBuffer(dev_.device_, tb);
        SDL_GPUTextureTransferInfo src{};
        src.transfer_buffer = tb;
        src.pixels_per_row = static_cast<Uint32>(rect.w);
        src.rows_per_layer = static_cast<Uint32>(rect.h);
        SDL_GPUTextureRegion dst{};
        dst.texture = t->texture;
        dst.mip_level = level;
        dst.x = static_cast<Uint32>(rect.x);
        dst.y = static_cast<Uint32>(rect.y);
        dst.w = static_cast<Uint32>(rect.w);
        dst.h = static_cast<Uint32>(rect.h);
        dst.d = 1;
        SDL_UploadToGPUTexture(copyPass(), &src, &dst, false);  // dirty rect: keep the rest
    }
    SDL_ReleaseGPUTransferBuffer(dev_.device_, tb);
}

void SdlCommandList::uploadBuffer(BufferH buffer, std::uint32_t offset, std::span<const std::byte> data) {
    SdlBuffer* b = dev_.buffers_.get(buffer);
    if (!b || data.empty() || offset + data.size() > b->desc.size) return;
    SDL_GPUTransferBufferCreateInfo tbInfo{};
    tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbInfo.size = static_cast<Uint32>(data.size());
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev_.device_, &tbInfo);
    if (!tb) return;
    if (void* mapped = SDL_MapGPUTransferBuffer(dev_.device_, tb, false)) {
        std::memcpy(mapped, data.data(), data.size());
        SDL_UnmapGPUTransferBuffer(dev_.device_, tb);
        SDL_GPUTransferBufferLocation src{tb, 0};
        SDL_GPUBufferRegion dst{b->buffer, offset, static_cast<Uint32>(data.size())};
        // A write from the start cycles the buffer, the explicit-API form of GL orphaning.
        SDL_UploadToGPUBuffer(copyPass(), &src, &dst, offset == 0);
    }
    SDL_ReleaseGPUTransferBuffer(dev_.device_, tb);
}

void SdlCommandList::copyTexture(TextureH src, const Rect& srcRect, TextureH dst, std::int32_t dstX,
                                 std::int32_t dstY) {
    SdlTexture* s = dev_.textures_.get(src);
    SdlTexture* d = dev_.textures_.get(dst);
    if (!s || !d) return;
    SDL_GPUTextureLocation from{s->texture, 0, 0, static_cast<Uint32>(srcRect.x), static_cast<Uint32>(srcRect.y), 0};
    SDL_GPUTextureLocation to{d->texture, 0, 0, static_cast<Uint32>(dstX), static_cast<Uint32>(dstY), 0};
    SDL_CopyGPUTextureToTexture(copyPass(), &from, &to, static_cast<Uint32>(srcRect.w),
                                static_cast<Uint32>(srcRect.h), 1, false);
}

// Never reached: createTexture refuses multisampled textures on this backend (Caps::maxSamples is 1).
void SdlCommandList::resolveTexture(TextureH, TextureH) {}

// SDL_BlitGPUTexture converts between colour formats (RGBA8 target -> BGRA8 swapchain) and scales.
void SdlCommandList::blitTexture(TextureH src, const Rect& srcRect, TextureH dst, const Rect& dstRect, Filter filter) {
    SdlTexture* s = dev_.textures_.get(src);
    SdlTexture* d = dev_.textures_.get(dst);
    if (!s || !d || srcRect.w <= 0 || srcRect.h <= 0 || dstRect.w <= 0 || dstRect.h <= 0) return;
    endRenderPass();
    endCopyPass();
    SDL_GPUBlitInfo info{};
    info.source.texture = s->texture;
    info.source.x = static_cast<Uint32>(srcRect.x);
    info.source.y = static_cast<Uint32>(srcRect.y);
    info.source.w = static_cast<Uint32>(srcRect.w);
    info.source.h = static_cast<Uint32>(srcRect.h);
    info.destination.texture = d->texture;
    info.destination.x = static_cast<Uint32>(dstRect.x);
    info.destination.y = static_cast<Uint32>(dstRect.y);
    info.destination.w = static_cast<Uint32>(dstRect.w);
    info.destination.h = static_cast<Uint32>(dstRect.h);
    info.load_op = SDL_GPU_LOADOP_LOAD;
    info.filter = filter == Filter::Linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    SDL_BlitGPUTexture(dev_.cmdbuf_, &info);
}

RenderPass* SdlCommandList::beginRenderPass(const RenderPassDesc& desc) {
    endRenderPass();
    endCopyPass();
    SdlTexture* c = dev_.textures_.get(desc.color.texture);
    if (!c) return nullptr;
    SDL_GPUColorTargetInfo color{};
    color.texture = c->texture;
    color.clear_color = {desc.color.clear[0], desc.color.clear[1], desc.color.clear[2], desc.color.clear[3]};
    color.load_op = sdlLoad(desc.color.load);
    color.store_op = sdlStore(desc.color.store);
    SDL_GPUDepthStencilTargetInfo depth{};
    SDL_GPUDepthStencilTargetInfo* depthPtr = nullptr;
    if (desc.depthStencil.texture) {
        SdlTexture* d = dev_.textures_.get(desc.depthStencil.texture);
        if (!d) return nullptr;
        depth.texture = d->texture;
        depth.clear_depth = desc.depthStencil.clearDepth;
        depth.clear_stencil = desc.depthStencil.clearStencil;
        depth.load_op = depth.stencil_load_op = sdlLoad(desc.depthStencil.load);
        depth.store_op = depth.stencil_store_op = sdlStore(desc.depthStencil.store);
        depthPtr = &depth;
    }
    dev_.pass_.pass = SDL_BeginGPURenderPass(dev_.cmdbuf_, &color, 1, depthPtr);
    if (!dev_.pass_.pass) return nullptr;
    const SDL_GPUViewport vp{0.f, 0.f, static_cast<float>(c->desc.width), static_cast<float>(c->desc.height), 0.f, 1.f};
    SDL_SetGPUViewport(dev_.pass_.pass, &vp);
    const SDL_Rect sc{0, 0, static_cast<int>(c->desc.width), static_cast<int>(c->desc.height)};
    SDL_SetGPUScissor(dev_.pass_.pass, &sc);
    return &dev_.pass_;
}

void SdlCommandList::endRenderPass() {
    if (!dev_.pass_.pass) return;
    SDL_EndGPURenderPass(dev_.pass_.pass);
    dev_.pass_.pass = nullptr;
}

TextureH SdlCommandList::acquireSwapchainTexture(std::uint32_t* width, std::uint32_t* height) {
    if (!dev_.windowClaimed_ || dev_.swapchain_) return dev_.swapchain_;
    SDL_GPUTexture* tex = nullptr;
    Uint32 w = 0, h = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(dev_.cmdbuf_, dev_.desc_.window, &tex, &w, &h) || !tex) return {};
    SdlTexture t;
    t.texture = tex;
    t.owned = false;
    t.desc.width = w;
    t.desc.height = h;
    t.desc.usage = kUsageColorTarget;
    dev_.swapchain_ = dev_.textures_.add(t);
    if (width) *width = w;
    if (height) *height = h;
    return dev_.swapchain_;
}

// ---- render pass ------------------------------------------------------------------------------------------

void SdlRenderPass::bindPipeline(PipelineH pipeline) {
    if (SdlPipeline* p = dev_.pipelines_.get(pipeline)) SDL_BindGPUGraphicsPipeline(pass, p->pipeline);
}

void SdlRenderPass::setViewport(const Viewport& v) {
    const SDL_GPUViewport vp{v.x, v.y, v.w, v.h, v.minDepth, v.maxDepth};
    SDL_SetGPUViewport(pass, &vp);
}

void SdlRenderPass::setScissor(const Rect& r) {
    const SDL_Rect sc{r.x, r.y, r.w, r.h};
    SDL_SetGPUScissor(pass, &sc);
}

void SdlRenderPass::setBlendConstants(const std::array<float, 4>& c) {
    SDL_SetGPUBlendConstants(pass, SDL_FColor{c[0], c[1], c[2], c[3]});
}

void SdlRenderPass::setStencilReference(std::uint8_t reference) { SDL_SetGPUStencilReference(pass, reference); }

void SdlRenderPass::bindVertexBuffer(BufferH buffer, std::uint32_t offset) {
    SdlBuffer* b = dev_.buffers_.get(buffer);
    if (!b) return;
    const SDL_GPUBufferBinding binding{b->buffer, offset};
    SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
}

void SdlRenderPass::bindIndexBuffer(BufferH buffer, IndexType type, std::uint32_t offset) {
    SdlBuffer* b = dev_.buffers_.get(buffer);
    if (!b) return;
    const SDL_GPUBufferBinding binding{b->buffer, offset};
    SDL_BindGPUIndexBuffer(pass, &binding,
                           type == IndexType::U32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT);
}

void SdlRenderPass::bindSampler(ShaderStage stage, std::uint32_t slot, TextureH texture, SamplerH sampler) {
    SdlTexture* t = dev_.textures_.get(texture);
    SdlSampler* s = dev_.samplers_.get(sampler);
    if (!t || !s) return;
    const SDL_GPUTextureSamplerBinding binding{t->texture, s->sampler};
    if (stage == ShaderStage::Vertex)
        SDL_BindGPUVertexSamplers(pass, slot, &binding, 1);
    else
        SDL_BindGPUFragmentSamplers(pass, slot, &binding, 1);
}

void SdlRenderPass::pushUniforms(ShaderStage stage, std::uint32_t slot, std::span<const std::byte> data) {
    if (stage == ShaderStage::Vertex)
        SDL_PushGPUVertexUniformData(dev_.cmdbuf_, slot, data.data(), static_cast<Uint32>(data.size()));
    else
        SDL_PushGPUFragmentUniformData(dev_.cmdbuf_, slot, data.data(), static_cast<Uint32>(data.size()));
}

void SdlRenderPass::draw(std::uint32_t vertexCount, std::uint32_t firstVertex) {
    if (!vertexCount) return;
    dev_.countDraw(vertexCount);
    SDL_DrawGPUPrimitives(pass, vertexCount, 1, firstVertex, 0);
}

void SdlRenderPass::drawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex, std::int32_t vertexOffset) {
    if (!indexCount) return;
    dev_.countDraw(indexCount);
    SDL_DrawGPUIndexedPrimitives(pass, indexCount, 1, firstIndex, vertexOffset, 0);
}

}  // namespace

void sdlGpuSetPresentMode(Device& device, bool vsync) {
    if (auto* d = dynamic_cast<SdlGpuDevice*>(&device)) d->setPresentMode(vsync);
}

Result<std::unique_ptr<Device>> createSdlGpuDevice(const SdlGpuDeviceDesc& desc) {
    auto device = std::make_unique<SdlGpuDevice>(desc);
    if (auto r = device->init(); !r) return Result<std::unique_ptr<Device>>::fail(r.error());
    return Result<std::unique_ptr<Device>>::ok(std::move(device));
}

}  // namespace sh::rhi
