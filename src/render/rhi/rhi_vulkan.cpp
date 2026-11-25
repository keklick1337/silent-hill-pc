// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#include "render/rhi/rhi_vulkan.h"

#include "render/rhi/rhi_vulkan_native.h"

#include "render/rhi/rhi_pool.h"
#include "render/rhi/rhi_vulkan_native.h"
#include "render/rhi/pipeline_store.h"
#include "render/rhi/shader_overrides.h"
#include "render/rhi/vk_pipeline_warm.h"
#include "render/rhi/vk_rendering.h"

// volk first: it declares the Vulkan API as function pointers (VK_NO_PROTOTYPES), which SDL_vulkan.h then reuses.
#include <volk.h>
#include <vk_mem_alloc.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sh::rhi {

namespace {

constexpr std::uint32_t kFrames = 2;
constexpr std::uint32_t kMaxQueries = 256;
constexpr std::uint32_t kMaxSamplerSlots = 16;
constexpr std::uint32_t kMaxUniformSlots = 8;
constexpr std::uint32_t kMaxStorageSlots = 4;
constexpr VkDeviceSize kUniformChunk = VkDeviceSize{4} << 20;
constexpr VkDeviceSize kStagingChunk = VkDeviceSize{8} << 20;
constexpr std::uint64_t kFenceTimeoutNs = 10'000'000'000ull;
// A hidden or occluded window may never give an image under FIFO: the frame is skipped instead of hanging.
constexpr std::uint64_t kAcquireTimeoutNs = 1'000'000'000ull;
constexpr Uint64 kBudgetPeriodMs = 250;
constexpr int kMaxValidationLogs = 200;

const char* resultName(VkResult r) {
    switch (r) {
        case VK_SUCCESS:
            return "VK_SUCCESS";
        case VK_NOT_READY:
            return "VK_NOT_READY";
        case VK_TIMEOUT:
            return "VK_TIMEOUT";
        case VK_SUBOPTIMAL_KHR:
            return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:
            return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:
            return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_LAYER_NOT_PRESENT:
            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:
            return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_SURFACE_LOST_KHR:
            return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:
            return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_OUT_OF_POOL_MEMORY:
            return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_ERROR_FRAGMENTED_POOL:
            return "VK_ERROR_FRAGMENTED_POOL";
        default:
            return "VkResult";
    }
}

std::string vkError(const char* what, VkResult r) {
    return std::string("vulkan: ") + what + ": " + resultName(r) + " (" + std::to_string(static_cast<int>(r)) + ")";
}

// Must be enabled on a device that lists it (MoltenVK): the name is behind VK_ENABLE_BETA_EXTENSIONS in the headers.
constexpr const char* kPortabilitySubset = "VK_KHR_portability_subset";

std::string versionText(std::uint32_t v) {
    return std::to_string(VK_API_VERSION_MAJOR(v)) + "." + std::to_string(VK_API_VERSION_MINOR(v)) + "." +
           std::to_string(VK_API_VERSION_PATCH(v));
}

std::uint32_t bytesPerTexel(TextureFormat f) {
    switch (f) {
        case TextureFormat::RGBA8:
            return 4;
        case TextureFormat::RG8:
            return 2;
        case TextureFormat::R8:
            return 1;
        case TextureFormat::D24S8:
            return 4;
        case TextureFormat::RGBA16F:
            return 8;
    }
    return 4;
}

const char* hdrBlockText(VulkanHdrBlock b) {
    switch (b) {
        case VulkanHdrBlock::None:
            return "";
        case VulkanHdrBlock::Off:
            return "off";
        case VulkanHdrBlock::OtherBackend:
            return "not the native Vulkan renderer";
        case VulkanHdrBlock::NoSurface:
            return "no window surface (offscreen)";
        case VulkanHdrBlock::NoColorspaceExtension:
            return "no VK_EXT_swapchain_colorspace";
        case VulkanHdrBlock::NoHdr10Format:
            return "the display offers no HDR10 format";
    }
    return "";
}

// IEEE half to float, for the readback of RGBA16F targets.
float halfToFloat(std::uint16_t h) {
    const std::uint32_t sign = (h >> 15) & 1u, exp = (h >> 10) & 0x1Fu, mant = h & 0x3FFu;
    float v;
    if (exp == 0)
        v = std::ldexp(static_cast<float>(mant), -24);
    else if (exp == 31)
        v = mant ? 0.f : 65504.f;  // NaN reads as black, infinity as the largest value
    else
        v = std::ldexp(static_cast<float>(mant | 0x400u), static_cast<int>(exp) - 25);
    return sign ? -v : v;
}

VkFormat vkVertexFormat(VertexFormat f) {
    switch (f) {
        case VertexFormat::Float:
            return VK_FORMAT_R32_SFLOAT;
        case VertexFormat::Float2:
            return VK_FORMAT_R32G32_SFLOAT;
        case VertexFormat::Float3:
            return VK_FORMAT_R32G32B32_SFLOAT;
        case VertexFormat::Float4:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case VertexFormat::Short4:
            return VK_FORMAT_R16G16B16A16_SINT;
        case VertexFormat::UByte4:
            return VK_FORMAT_R8G8B8A8_UINT;
        case VertexFormat::UByte4Norm:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case VertexFormat::Byte4:
            return VK_FORMAT_R8G8B8A8_SINT;
    }
    return VK_FORMAT_R32G32B32A32_SFLOAT;
}

VkBlendFactor vkBlendFactor(BlendFactor f) {
    switch (f) {
        case BlendFactor::Zero:
            return VK_BLEND_FACTOR_ZERO;
        case BlendFactor::One:
            return VK_BLEND_FACTOR_ONE;
        case BlendFactor::SrcColor:
            return VK_BLEND_FACTOR_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::DstColor:
            return VK_BLEND_FACTOR_DST_COLOR;
        case BlendFactor::OneMinusDstColor:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case BlendFactor::SrcAlpha:
            return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha:
            return VK_BLEND_FACTOR_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case BlendFactor::ConstantColor:
            return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case BlendFactor::OneMinusConstantColor:
            return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    }
    return VK_BLEND_FACTOR_ONE;
}

VkBlendOp vkBlendOp(BlendOp op) {
    switch (op) {
        case BlendOp::Add:
            return VK_BLEND_OP_ADD;
        case BlendOp::Subtract:
            return VK_BLEND_OP_SUBTRACT;
        case BlendOp::ReverseSubtract:
            return VK_BLEND_OP_REVERSE_SUBTRACT;
        case BlendOp::Min:
            return VK_BLEND_OP_MIN;
        case BlendOp::Max:
            return VK_BLEND_OP_MAX;
    }
    return VK_BLEND_OP_ADD;
}

VkCompareOp vkCompare(CompareOp op) {
    switch (op) {
        case CompareOp::Never:
            return VK_COMPARE_OP_NEVER;
        case CompareOp::Less:
            return VK_COMPARE_OP_LESS;
        case CompareOp::Equal:
            return VK_COMPARE_OP_EQUAL;
        case CompareOp::LessEqual:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::Greater:
            return VK_COMPARE_OP_GREATER;
        case CompareOp::NotEqual:
            return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::GreaterEqual:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::Always:
            return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_ALWAYS;
}

VkStencilOp vkStencilOp(StencilOp op) {
    switch (op) {
        case StencilOp::Keep:
            return VK_STENCIL_OP_KEEP;
        case StencilOp::Zero:
            return VK_STENCIL_OP_ZERO;
        case StencilOp::Replace:
            return VK_STENCIL_OP_REPLACE;
        case StencilOp::IncrementClamp:
            return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case StencilOp::DecrementClamp:
            return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case StencilOp::Invert:
            return VK_STENCIL_OP_INVERT;
        case StencilOp::IncrementWrap:
            return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case StencilOp::DecrementWrap:
            return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    }
    return VK_STENCIL_OP_KEEP;
}

VkAttachmentLoadOp vkLoad(LoadOp op) {
    switch (op) {
        case LoadOp::Load:
            return VK_ATTACHMENT_LOAD_OP_LOAD;
        case LoadOp::Clear:
            return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case LoadOp::DontCare:
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_LOAD_OP_LOAD;
}

VkAttachmentStoreOp vkStore(StoreOp op) {
    return op == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

bool hasStencil(VkFormat f) {
    return f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT || f == VK_FORMAT_D16_UNORM_S8_UINT;
}

bool hasExtension(const std::vector<VkExtensionProperties>& list, const char* name) {
    for (const VkExtensionProperties& e : list)
        if (std::strcmp(e.extensionName, name) == 0) return true;
    return false;
}

// Host-visible memory handed out front to back during one frame and reused once the frame's fence has signalled:
// upload staging and the uniform data of the draws.
struct HostChunk {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation alloc = nullptr;
    std::byte* mapped = nullptr;
    VkDeviceSize size = 0;
};

struct Ring {
    std::vector<HostChunk> chunks;
    std::uint32_t current = 0;
    VkDeviceSize offset = 0;
    bool used = false;  // anything handed out since reset
};

struct Slice {
    std::uint32_t chunk = 0;
    VkDeviceSize offset = 0;
    std::byte* ptr = nullptr;
    VkBuffer buffer = VK_NULL_HANDLE;
};

struct VkTexture {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;         // every aspect: the attachment view
    VkImageView sampledView = VK_NULL_HANDLE;  // depth aspect only for a sampled depth texture, else `view`
    VmaAllocation alloc = nullptr;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    TextureDesc desc;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    // Where the texture waits between uses: sampled textures in the shader-read layout, so a draw can sample them
    // without a transition inside the render pass.
    VkImageLayout rest = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bool swapchain = false;
    bool external = false;  // vulkanAdoptImage: the image is its owner's
    std::uint64_t bytes = 0;
    std::uint64_t usedIn = 0;  // the last submission whose main command buffer refers to the image
};

struct VkBuf {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation alloc = nullptr;
    BufferDesc desc;
    std::uint64_t usedIn = 0;  // the last submission whose main command buffer refers to the buffer
    bool pinned = false;       // handed out natively (rhi_vulkan_native.h): never replaced
    // Earlier contents of a buffer rewritten from offset 0 after a draw of the same frame read it (cycling), each free
    // once the submission `busy` is done.
    struct Spare {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation alloc = nullptr;
        std::uint64_t busy = 0;
    };
    std::vector<Spare> spares;
};

// An upload into a buffer or an image that the frame's commands have not used yet: recorded ahead of them (into the
// preamble), so it does not break the render pass it was asked for in the middle of.
struct HoistedCopy {
    VkBuffer src = VK_NULL_HANDLE;
    VkDeviceSize srcOffset = 0;
    VkBuffer dst = VK_NULL_HANDLE;  // a buffer copy, or an image one:
    VkDeviceSize dstOffset = 0;
    VkDeviceSize size = 0;
    VkImage image = VK_NULL_HANDLE;
    VkImageAspectFlags aspect = 0;
    VkImageLayout rest = VK_IMAGE_LAYOUT_UNDEFINED;
    VkBufferImageCopy region{};
};

struct VkSamp {
    VkSampler sampler = VK_NULL_HANDLE;
};

struct ShaderModule {
    VkShaderModule module = VK_NULL_HANDLE;
    std::uint64_t hash = 0;  // of `code`: how pipeline recipes name the shader (vk_pipeline_warm.h)
    vkn::SpirvCode code;
};

struct VkShader {
    std::shared_ptr<ShaderModule> module;
    ShaderStage stage = ShaderStage::Vertex;
    std::uint32_t samplers = 0;
    std::uint32_t uniforms = 0;
    std::uint32_t storage = 0;
    std::string entry;
};

// The SDL3 GPU resource layout: set 0 vertex samplers, 1 vertex uniforms, 2 fragment samplers, 3 fragment uniforms;
// the backend's own set 4 holds the storage buffers. Sets 0, 1 and 4 reach every stage before rasterisation.
enum SetKind : std::uint32_t {
    kVertexSamplers = 0,
    kVertexUniforms = 1,
    kFragmentSamplers = 2,
    kFragmentUniforms = 3,
    kStorageBuffers = 4
};
constexpr std::uint32_t kSetKinds = 5;

struct VkPipe {
    PipelineDesc desc;
    std::vector<VertexAttribute> attributes;
    std::string name;
    std::shared_ptr<ShaderModule> vs, fs;
    std::string vsEntry, fsEntry;
    // Tessellation and mesh stages (PipelineDesc); `mesh` replaces the vertex stage.
    std::shared_ptr<ShaderModule> tcs, tes, task, mesh;
    std::string tcsEntry, tesEntry, taskEntry, meshEntry;
    std::array<std::uint32_t, kSetKinds> counts{};
    std::array<VkDescriptorSetLayout, kSetKinds> setLayouts{};
    VkPipelineLayout layout = VK_NULL_HANDLE;
    // Pipelines are baked per attachment format (dynamic rendering): the swapchain is BGRA8 where targets are RGBA8.
    struct Variant {
        VkFormat color = VK_FORMAT_UNDEFINED;
        VkFormat depth = VK_FORMAT_UNDEFINED;
        VkPipeline pipeline = VK_NULL_HANDLE;
        std::uint64_t key = 0;  // the recipe's (VulkanDevice::shared_)
    };
    std::vector<Variant> variants;
};

struct Span {
    std::array<char, 24> name{};
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
};

struct Frame {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBuffer preamble = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> descPools;
    std::uint32_t descPool = 0;
    Ring staging;
    Ring uniforms;
    std::unordered_map<std::uint64_t, VkDescriptorSet> uniformSets;
    VkQueryPool queries = VK_NULL_HANDLE;
    std::uint32_t queryCount = 0;
    std::vector<Span> spans;
    bool timed = false;
    std::uint64_t serial = 0;  // the submission this slot last carried
};

struct PendingInit {
    VkImage image;
    VkImageAspectFlags aspect;
    VkImageLayout layout;
};

class VulkanDevice;

class VulkanPass final : public RenderPass {
public:
    explicit VulkanPass(VulkanDevice& device) : dev_(device) {}
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
    void bindStorageBuffer(std::uint32_t slot, BufferH buffer, std::uint32_t offset) override;
    void drawMeshTasks(std::uint32_t taskGroups) override;
    void beginTimedSection(std::string_view name) override;
    void endTimedSection() override;

private:
    VulkanDevice& dev_;
};

class VulkanList final : public CommandList {
public:
    explicit VulkanList(VulkanDevice& device) : dev_(device) {}
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

private:
    VulkanDevice& dev_;
};

class VulkanDevice final : public Device {
public:
    friend struct NativeAccess;  // rhi_vulkan_native.h
    explicit VulkanDevice(const VulkanDeviceDesc& desc) : desc_(desc), list_(*this), pass_(*this) {}
    ~VulkanDevice() override;

    Result<void> init();

    Backend backend() const override { return Backend::Vulkan; }
    const Caps& caps() const override { return caps_; }
    const VulkanCaps& vulkanCaps() const { return vkCaps_; }
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
    void waitIdle() override;
    void setProfiling(bool on) override { profiling_ = on; }
    void setPassTiming(bool on) override { passTiming_ = on; }
    ProfileStats takeProfileStats() override;
    void traceFrames(int frames) override { traceFrames_ = std::max(frames, 0); }
    void preparePipeline(const PipelineDesc& desc) override;
    PipelineWarmup pipelineWarmup() const override;

    // rhi_vulkan_native.h.
    VulkanNative native() const {
        return {instance_, physical_, device_, queue_, queueFamily_, allocator_, apiVersion_, rayTracing_};
    }
    VkCommandBuffer nativeCommands() {
        if (!recording_) return VK_NULL_HANDLE;
        closePass();
        leaveTransfer();
        return frame().cmd;
    }
    std::uint32_t frameSlot() const { return frameIndex_; }
    void retireNative(std::function<void()> release) { retire(std::move(release)); }
    bool imageInfo(TextureH h, VulkanImageInfo* out) {
        VkTexture* t = textures_.get(h);
        if (!t) return false;
        t->usedIn = recordingSerial();
        *out = {t->image, t->sampledView, t->format, t->rest, t->desc.width, t->desc.height};
        return true;
    }
    VkBuffer nativeBuffer(BufferH h) {
        VkBuf* b = buffers_.get(h);
        if (!b) return VK_NULL_HANDLE;
        b->pinned = true;
        b->usedIn = recordingSerial();
        return b->buffer;
    }
    TextureH adoptImage(VkImage image, VkImageView view, VkFormat format, std::uint32_t w, std::uint32_t h) {
        VkTexture t;
        t.image = image;
        t.view = t.sampledView = view;
        t.format = format;
        t.desc.width = w;
        t.desc.height = h;
        t.desc.usage = kUsageSampled;
        t.layout = t.rest = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        t.external = true;
        return textures_.add(t);
    }
    void beginNativeSpan(const char* name) {
        if (recording_) beginSpan(name, std::strlen(name));
    }
    void endNativeSpan() {
        if (recording_) endSpan();
    }

    void setPresentMode(bool vsync) {
        if (vsync == desc_.vsync) return;
        desc_.vsync = vsync;
        swapchainDirty_ = true;
    }
    void setFramePacing(const VulkanFramePacing& p) {
        if (p.presentMode != pacing_.presentMode && surface_) swapchainDirty_ = true;
        pacing_ = p;
    }
    bool presentWaitSupported() const { return presentWait_; }
    void setHdrOutput(const VulkanHdrOutput& hdr) {
        const bool changed = hdr.enabled != hdr_.enabled;
        const bool metadata = hdr.paperWhiteNits != hdr_.paperWhiteNits || hdr.peakNits != hdr_.peakNits;
        hdr_ = hdr;
        if (!surface_) {
            hdrBlock_ = hdr.enabled ? VulkanHdrBlock::NoSurface : VulkanHdrBlock::Off;
            return;
        }
        if (changed)
            swapchainDirty_ = true;
        else if (metadata && hdrActive_ && hdrMetadata_)
            applyHdrMetadata();
    }
    VulkanHdrStatus hdrStatus() const {
        VulkanHdrStatus s;
        s.active = hdrActive_ && swapchain_ != VK_NULL_HANDLE;
        s.block = s.active ? VulkanHdrBlock::None : hdrBlock_;
        s.reason = hdrBlockText(s.block);
        return s;
    }
    void applyHdrMetadata();

private:
    friend class VulkanPass;
    friend class VulkanList;

    // ---- setup
    Result<void> createInstance();
    Result<void> pickPhysicalDevice();
    Result<void> createLogicalDevice();
    Result<void> createFrames();
    Result<void> createSwapchain();
    void destroySwapchain();
    void fillCaps();

    // ---- helpers
    VkFormat textureFormat(TextureFormat f) const {
        switch (f) {
            case TextureFormat::RGBA8:
                return VK_FORMAT_R8G8B8A8_UNORM;
            case TextureFormat::RG8:
                return VK_FORMAT_R8G8_UNORM;
            case TextureFormat::R8:
                return VK_FORMAT_R8_UNORM;
            case TextureFormat::D24S8:
                return depthFormat_;
            case TextureFormat::RGBA16F:
                return VK_FORMAT_R16G16B16A16_SFLOAT;
        }
        return VK_FORMAT_R8G8B8A8_UNORM;
    }
    Frame& frame() { return frames_[frameIndex_]; }
    void retire(std::function<void()> release);
    void collectGarbage();
    bool allocHost(Ring& ring, VkBufferUsageFlags usage, VkDeviceSize chunkSize, VkDeviceSize size, VkDeviceSize align,
                   VkDeviceSize reserve, Slice* out);
    void resetRing(Ring& ring) {
        ring.current = 0;
        ring.offset = 0;
        ring.used = false;
    }
    void flushRing(Ring& ring);
    void freeRing(Ring& ring);
    VkDescriptorSetLayout setLayout(SetKind kind, std::uint32_t count);
    VkPipelineLayout pipelineLayout(const std::array<std::uint32_t, kSetKinds>& counts,
                                    const std::array<VkDescriptorSetLayout, kSetKinds>& layouts);
    VkDescriptorSet allocSet(VkDescriptorSetLayout layout);
    VkPipeline variant(VkPipe& p, VkFormat color, VkFormat depth);
    Result<VkPipe> makePipe(const PipelineDesc& desc);
    vkn::GraphicsRecipe recipeFor(const VkPipe& p, VkFormat color, VkFormat depth) const;
    vkn::BuildTarget buildTarget(const vkn::GraphicsRecipe& recipe);
    // The pipeline cache on disk and the manifest of the pipelines made (RENDER.md section 2.2d).
    void loadPipelineStore();
    void startPrewarm();
    void notePipeline(std::uint64_t key, std::vector<std::uint8_t> bytes, const vkn::GraphicsRecipe& recipe,
                      const VkPipe& p);
    void savePipelineStore(bool wait);
    void pipelineStoreFrame();
    void barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to);
    void transition(VkTexture& t, VkImageLayout to) {
        barrier(frame().cmd, t.image, t.aspect, t.layout, to);
        t.layout = to;
    }
    void memoryBarrier();
    void closePass();
    void enterTransfer();
    void leaveTransfer();
    void touch(TextureH h);
    void beginSpan(const char* name, std::size_t length);
    void endSpan();
    void collectTimings(Frame& f);
    bool flushDraw();
    void pushLiveUniforms();
    Result<void> markLost(const char* where, VkResult r) {
        if (r == VK_ERROR_DEVICE_LOST) lost_ = true;
        return Result<void>::fail(vkError(where, r));
    }
    Result<TextureH> makeImage(const TextureDesc& desc, VkFormat format, VkImageUsageFlags usage);
    void releaseTexture(VkTexture& t);
    void recordPendingInits(VkCommandBuffer cmd);
    TextureH surfacelessTarget(std::uint32_t w, std::uint32_t h);

    VulkanDeviceDesc desc_;
    Caps caps_;
    VulkanCaps vkCaps_;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    // Android gives the activity a new window each time it comes back to the front (from another app, or from the
    // system's file picker): the surface of the old one only feeds an abandoned queue, so it is made again.
    std::atomic<bool> surfaceStale_{false};
#ifdef __ANDROID__
    static bool SDLCALL onAppEvent(void* self, SDL_Event* event);
#endif
    void renewSurface();
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props_{};
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t queueFamily_ = 0;
    VmaAllocator allocator_ = nullptr;
    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
    std::uint32_t apiVersion_ = VK_API_VERSION_1_1;
    bool fillModeNonSolid_ = false;
    bool memoryBudget_ = false;
    bool rayTracing_ = false;
    std::vector<VkExtensionProperties> deviceExtensions_;
    VkFormat depthFormat_ = VK_FORMAT_D24_UNORM_S8_UINT;
    VkDeviceSize uboAlign_ = 256;
    VkDeviceSize uboRange_ = 16384;
    double timestampPeriodNs_ = 1.0;
    std::uint64_t timestampMask_ = ~0ull;

    // Dynamic rendering and synchronization2 when the device has them, render passes and Vulkan 1.0 barriers else.
    bool hasDynamicRendering_ = false, hasSync2_ = false;  // of the chosen device, before SH_VK_COMPAT
    vkn::Rendering passApi_;

    detail::Pool<TextureH, VkTexture> textures_;
    detail::Pool<BufferH, VkBuf> buffers_;
    detail::Pool<SamplerH, VkSamp> samplers_;
    detail::Pool<ShaderH, VkShader> shaders_;
    detail::Pool<PipelineH, VkPipe> pipelines_;
    std::unordered_map<std::uint64_t, VkDescriptorSetLayout> setLayouts_;
    std::unordered_map<std::uint64_t, VkPipelineLayout> pipelineLayouts_;

    std::array<Frame, kFrames> frames_{};
    std::uint32_t frameIndex_ = 0;
    std::uint64_t submitted_ = 0;  // submissions so far
    std::uint64_t completed_ = 0;  // submissions the GPU finished
    struct Garbage {
        std::uint64_t serial;
        std::function<void()> release;
    };
    std::deque<Garbage> garbage_;
    std::vector<PendingInit> pendingInits_;

    // One-shot work outside the frame (readback).
    VkCommandPool oneShotPool_ = VK_NULL_HANDLE;
    VkCommandBuffer oneShot_ = VK_NULL_HANDLE;
    VkFence oneShotFence_ = VK_NULL_HANDLE;

    // Swapchain.
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapExtent_{};
    std::vector<TextureH> swapTextures_;
    std::vector<VkSemaphore> renderDone_;
    bool swapchainDirty_ = false;
    // The swapchain is not in the surface's current transform (a landscape game on a phone whose screen is portrait
    // by nature): Android answers every present with VK_SUBOPTIMAL_KHR then and the compositor turns the picture.
    // Remaking the swapchain would change nothing but cost a wait for the idle device each frame; a new size still
    // remakes it (acquireSwapchainTexture compares the window's).
    bool swapRotated_ = false;
    VulkanHdrOutput hdr_;                            // what the engine asks for
    bool hdrActive_ = false;                         // the swapchain is HDR10 (ST 2084, BT.2020)
    VulkanHdrBlock hdrBlock_ = VulkanHdrBlock::Off;  // why not, while it is not
    bool hdrMetadata_ = false;                       // VK_EXT_hdr_metadata enabled
    VulkanFramePacing pacing_;
    bool presentWait_ = false;     // VK_KHR_present_id and VK_KHR_present_wait enabled
    std::uint64_t presentId_ = 0;  // the last id handed to a present
    std::uint64_t shownFrom_ = 0;  // ids up to this one belong to an older swapchain
    std::uint32_t swapIndex_ = 0;
    TextureH acquired_;         // this frame's swapchain or surfaceless image
    bool acquireWait_ = false;  // the submission waits for the frame's acquire semaphore
    TextureH ownTarget_;        // surfaceless: the stand-in for the swapchain image

    TextureH dummy_;  // what an unbound sampler slot reads
    VkSampler dummySampler_ = VK_NULL_HANDLE;

    // Recording state.
    bool recording_ = false;
    bool inPass_ = false;
    // endRenderPass leaves the GPU's pass open: a next pass on the same attachments continues it (a tile-based GPU
    // keeps them in its tile memory), anything else ends it first (closePass).
    bool lingering_ = false;
    bool passStoresColor_ = false, passStoresDepth_ = false;
    // A pass begins on the GPU at its first draw (or at its end, for a clear): a clear asked for before that becomes
    // the pass's load op, and a pass with no draw and no clear is no pass at all.
    bool rendering_ = false;
    VkRenderingAttachmentInfo pendColor_{}, pendDepth_{};
    bool pendDepthOn_ = false, pendStencil_ = false;
    VkExtent2D pendExtent_{};
    // The pass merging, the late begin, the hoisted uploads and buffer cycling; SH_VK_NO_MERGE=1 turns them off (a
    // comparison of the picture and the counts).
    bool restructure_ = true;
    std::vector<HoistedCopy> hoisted_;
    std::uint64_t recordingSerial() const { return submitted_ + 1; }
    void beginRendering();
    void recordHoisted(VkCommandBuffer cmd);
    bool cycleBuffer(VkBuf& b);
    bool inTransfer_ = false;
    bool lost_ = false;
    TextureH passColor_, passDepth_;
    VkFormat passColorFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat passDepthFormat_ = VK_FORMAT_UNDEFINED;
    std::vector<TextureH> touched_;
    VkPipe* boundPipe_ = nullptr;
    PipelineH boundPipeH_;
    VkPipeline boundVk_ = VK_NULL_HANDLE;
    struct SamplerBinding {
        TextureH texture;
        SamplerH sampler;
    };
    std::array<std::array<SamplerBinding, kMaxSamplerSlots>, 2> samplerBind_{};
    std::array<bool, 2> samplersDirty_{true, true};
    std::array<std::array<std::uint32_t, kMaxUniformSlots>, 2> uboOffset_{};
    std::array<std::array<std::vector<std::byte>, kMaxUniformSlots>, 2> uboData_{};
    std::uint32_t uboChunk_ = 0;
    bool uniformsDirty_ = true;
    struct StorageBinding {
        BufferH buffer;
        std::uint32_t offset = 0;
    };
    std::array<StorageBinding, kMaxStorageSlots> storageBind_{};
    bool storageDirty_ = true;
    // Stages that see sets 0, 1 and 4: the vertex stage plus the enabled tessellation and mesh stages.
    VkShaderStageFlags preRasterStages_ = VK_SHADER_STAGE_VERTEX_BIT;
    bool tessellation_ = false;
    bool meshShaders_ = false;
    std::string meshShadersMissing_;
    std::int32_t openSpan_ = -1;
    std::int32_t openSection_ = -1;  // RenderPass::beginTimedSection, inside the open pass span

    // Profiling.
    bool profiling_ = false;
    bool passTiming_ = true;
    std::uint32_t drawCalls_ = 0;
    std::uint64_t triangles_ = 0;
    std::uint64_t patchTriangles_ = 0;
    std::int32_t pipelinesBuilt_ = 0;
    double pipelineBuildMs_ = 0.0;

    // Pipelines before their first use (RENDER.md section 2.2d).
    vkn::PipelineWarmer warmer_;
    std::filesystem::path storeDir_;  // empty: the cache stays in memory
    pipeline_store::Identity storeId_;
    bool cacheValid_ = false;          // the cache file matched this build and driver
    std::uint32_t prewarmQueued_ = 0;  // recipes of the manifest queued at start
    bool prewarmLogged_ = false;
    bool storeReady_ = false;  // the device started: a device that failed to start writes nothing
    Uint64 prewarmStartMs_ = 0;
    pipeline_store::Manifest earlier_;  // read at start, read-only after
    std::mutex storeMutex_;             // created_, also reached from the app event watch on Android
    pipeline_store::Manifest created_;
    std::unordered_set<std::uint64_t> createdKeys_;
    std::atomic<bool> storeDirty_{false};
    std::atomic<bool> saving_{false};
    std::mutex saveMutex_;  // one write of the files at a time
    Uint64 lastNewPipelineMs_ = 0;
    // First-use builds and pipelines taken from the prewarm since the last log line, and in all.
    struct BuildLog {
        std::uint32_t built = 0, taken = 0;
        double ms = 0.0, longestMs = 0.0;
        std::string longest;
    } buildLog_;
    Uint64 buildLogMs_ = 0;
    // The device's graphics pipelines by recipe key, shared by the VkPipes of identical descriptions.
    struct SharedPipeline {
        VkPipeline pipeline = VK_NULL_HANDLE;
        std::uint32_t refs = 0;
    };
    std::unordered_map<std::uint64_t, SharedPipeline> shared_;
    std::uint64_t textureUploadBytes_ = 0;
    double gpuMs_ = 0.0;
    bool gpuTimed_ = false;
    std::array<ProfileStats::PassTime, ProfileStats::kMaxPassTimes> passTimes_{};
    std::uint32_t passTimeCount_ = 0;
    std::uint64_t appVramBytes_ = 0;
    Uint64 budgetAtMs_ = 0;
    std::int64_t budgetKb_ = -1;
    int validationLogs_ = 0;
    ProfileStats::Counts counts_{};
    // traceFrames: the frames still to log, and the lines of the one being recorded.
    int traceFrames_ = 0;
    bool tracing_ = false;
    std::string trace_;
    std::uint32_t passDraws_ = 0;  // draws of the open pass (the trace)
    void note(const char* fmt, ...);

    VulkanList list_;
    VulkanPass pass_;

    static VKAPI_ATTR VkBool32 VKAPI_CALL onValidation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                       VkDebugUtilsMessageTypeFlagsEXT,
                                                       const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
        auto* self = static_cast<VulkanDevice*>(user);
        if (self->validationLogs_++ < kMaxValidationLogs && data && data->pMessage) {
            const bool error = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0;
            shader_log(1, std::string("vulkan validation ") + (error ? "error: " : "warning: ") + data->pMessage);
        }
        return VK_FALSE;
    }
};

// ---- setup ------------------------------------------------------------------------------------------------------

#ifdef __ANDROID__
bool SDLCALL VulkanDevice::onAppEvent(void* self, SDL_Event* event) {
    auto* device = static_cast<VulkanDevice*>(self);
    if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND) device->surfaceStale_ = true;
    // The system may end a process in the background without a word: what was built is written now.
    if (event->type == SDL_EVENT_WILL_ENTER_BACKGROUND && device->storeDirty_) device->savePipelineStore(true);
    return true;
}
#endif

void VulkanDevice::renewSurface() {
    vkDeviceWaitIdle(device_);
    completed_ = submitted_;
    destroySwapchain();
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(desc_.window, instance_, nullptr, &surface_)) {
        surface_ = VK_NULL_HANDLE;
        shader_log(1, std::string("vulkan: the window's surface cannot be made again: ") + SDL_GetError());
    }
    swapchainDirty_ = true;
}

VulkanDevice::~VulkanDevice() {
#ifdef __ANDROID__
    SDL_RemoveEventWatch(&VulkanDevice::onAppEvent, this);
#endif
    if (!device_) {
        if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
        if (messenger_) vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
        if (instance_) vkDestroyInstance(instance_, nullptr);
        return;
    }
    warmer_.stop();
    savePipelineStore(true);
    vkDeviceWaitIdle(device_);
    completed_ = submitted_ + 1;
    collectGarbage();
    destroySwapchain();
    if (ownTarget_) destroy(ownTarget_);
    if (dummy_) destroy(dummy_);
    for (const auto& [key, sp] : shared_) vkDestroyPipeline(device_, sp.pipeline, nullptr);
    shared_.clear();
    // Shader modules die with the last pipeline or shader that holds them.
    pipelines_ = {};
    shaders_ = {};
    collectGarbage();
    samplers_.forEach([&](VkSamp& s) { vkDestroySampler(device_, s.sampler, nullptr); });
    buffers_.forEach([&](VkBuf& b) { vmaDestroyBuffer(allocator_, b.buffer, b.alloc); });
    textures_.forEach([&](VkTexture& t) { releaseTexture(t); });
    collectGarbage();
    for (auto& [key, layout] : pipelineLayouts_) vkDestroyPipelineLayout(device_, layout, nullptr);
    for (auto& [key, layout] : setLayouts_) vkDestroyDescriptorSetLayout(device_, layout, nullptr);
    if (dummySampler_) vkDestroySampler(device_, dummySampler_, nullptr);
    for (Frame& f : frames_) {
        for (VkDescriptorPool p : f.descPools) vkDestroyDescriptorPool(device_, p, nullptr);
        freeRing(f.staging);
        freeRing(f.uniforms);
        if (f.queries) vkDestroyQueryPool(device_, f.queries, nullptr);
        if (f.fence) vkDestroyFence(device_, f.fence, nullptr);
        if (f.acquired) vkDestroySemaphore(device_, f.acquired, nullptr);
        if (f.pool) vkDestroyCommandPool(device_, f.pool, nullptr);
    }
    if (oneShotFence_) vkDestroyFence(device_, oneShotFence_, nullptr);
    if (oneShotPool_) vkDestroyCommandPool(device_, oneShotPool_, nullptr);
    if (pipelineCache_) vkDestroyPipelineCache(device_, pipelineCache_, nullptr);
    if (allocator_) vmaDestroyAllocator(allocator_);
    passApi_.destroy();
    vkDestroyDevice(device_, nullptr);
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (messenger_) vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
    vkDestroyInstance(instance_, nullptr);
}

Result<void> VulkanDevice::createInstance() {
#ifdef __APPLE__
    // The instance comes from the library SDL makes the surface with (loadVulkanLibrary).
    if (!loadVulkanLibrary())
        return Result<void>::fail("vulkan: no MoltenVK or Vulkan loader (brew install molten-vk, or the Vulkan SDK)");
    volkInitializeCustom(reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr()));
#else
    if (VkResult r = volkInitialize(); r != VK_SUCCESS) return Result<void>::fail(vkError("no Vulkan loader", r));
#endif
    std::uint32_t loaderVersion = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion) vkEnumerateInstanceVersion(&loaderVersion);
    if (loaderVersion < VK_API_VERSION_1_1)
        return Result<void>::fail("vulkan: the loader offers Vulkan " + versionText(loaderVersion) + ", 1.1 is needed");
    apiVersion_ = std::min<std::uint32_t>(loaderVersion, VK_API_VERSION_1_3);

    std::uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());

    std::vector<const char*> extensions;
    const bool wantSurface = desc_.window && !desc_.surfaceless;
    if (wantSurface) {
        Uint32 sdlCount = 0;
        const char* const* sdl = SDL_Vulkan_GetInstanceExtensions(&sdlCount);
        if (!sdl) return Result<void>::fail(std::string("vulkan: SDL_Vulkan_GetInstanceExtensions: ") + SDL_GetError());
        for (Uint32 i = 0; i < sdlCount; ++i) extensions.push_back(sdl[i]);
        if (hasExtension(available, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME)) {
            extensions.push_back(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
            vkCaps_.swapchainColorspace = true;
        }
    }
    std::vector<const char*> layers;
    if (desc_.validation) {
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        std::vector<VkLayerProperties> props(count);
        vkEnumerateInstanceLayerProperties(&count, props.data());
        const bool layer = std::any_of(props.begin(), props.end(), [](const VkLayerProperties& l) {
            return std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0;
        });
        if (layer && hasExtension(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            vkCaps_.validation = true;
        } else {
            shader_log(1, "vulkan: validation asked for, but VK_LAYER_KHRONOS_validation is not installed");
        }
    }

#ifdef __APPLE__
    // MoltenVK is a portability driver: the loader lists it only to an instance that asks for those.
    const bool portability = hasExtension(available, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    if (portability) extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Silent Hill PC";
    app.pEngineName = "Silent Hill PC";
    app.apiVersion = apiVersion_;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    info.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    info.ppEnabledLayerNames = layers.data();
#ifdef __APPLE__
    if (portability) info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    if (VkResult r = vkCreateInstance(&info, nullptr, &instance_); r != VK_SUCCESS)
        return Result<void>::fail(vkError("vkCreateInstance", r));
    volkLoadInstanceOnly(instance_);

    if (vkCaps_.validation) {
        VkDebugUtilsMessengerCreateInfoEXT m{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        m.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        m.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        m.pfnUserCallback = &VulkanDevice::onValidation;
        m.pUserData = this;
        vkCreateDebugUtilsMessengerEXT(instance_, &m, nullptr, &messenger_);
    }
    if (wantSurface && !SDL_Vulkan_CreateSurface(desc_.window, instance_, nullptr, &surface_))
        return Result<void>::fail(std::string("vulkan: SDL_Vulkan_CreateSurface: ") + SDL_GetError());
#ifdef __ANDROID__
    if (surface_) SDL_AddEventWatch(&VulkanDevice::onAppEvent, this);
#endif
    vkCaps_.surfaceless = surface_ == VK_NULL_HANDLE;
    return Result<void>::ok();
}

Result<void> VulkanDevice::pickPhysicalDevice() {
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());
    int bestScore = -1;
    std::string why = "no Vulkan device";
    for (VkPhysicalDevice pd : devices) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(pd, &p);
        const std::string name = p.deviceName;
        if (p.apiVersion < VK_API_VERSION_1_1) {
            why = name + " offers only Vulkan " + versionText(p.apiVersion);
            continue;
        }
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU && !desc_.allowCpu) {
            why = "only a CPU device, " + name;
            continue;
        }
        std::uint32_t n = 0;
        vkEnumerateDeviceExtensionProperties(pd, nullptr, &n, nullptr);
        std::vector<VkExtensionProperties> exts(n);
        vkEnumerateDeviceExtensionProperties(pd, nullptr, &n, exts.data());
        const bool core13 = p.apiVersion >= VK_API_VERSION_1_3 && apiVersion_ >= VK_API_VERSION_1_3;
        bool dynamicRendering = false, sync2 = false;
        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceDynamicRenderingFeaturesKHR fdr{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
        VkPhysicalDeviceSynchronization2FeaturesKHR fs2{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR};
        VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        if (core13) {
            f2.pNext = &f13;
        } else {
            if (hasExtension(exts, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) {
                fdr.pNext = f2.pNext;
                f2.pNext = &fdr;
            }
            if (hasExtension(exts, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)) {
                fs2.pNext = f2.pNext;
                f2.pNext = &fs2;
            }
        }
        vkGetPhysicalDeviceFeatures2(pd, &f2);
        // Without them (Vulkan 1.1 phones) passes and barriers take the Vulkan 1.0 path (vk_rendering.h).
        dynamicRendering = core13 ? f13.dynamicRendering : fdr.dynamicRendering;
        sync2 = core13 ? f13.synchronization2 : fs2.synchronization2;
        if (surface_ && !hasExtension(exts, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            why = name + " has no swapchain";
            continue;
        }
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, nullptr);
        std::vector<VkQueueFamilyProperties> families(n);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, families.data());
        std::optional<std::uint32_t> family;
        for (std::uint32_t i = 0; i < n && !family; ++i) {
            if (!(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            VkBool32 present = VK_TRUE;
            if (surface_) vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface_, &present);
            if (present) family = i;
        }
        if (!family) {
            why = name + " has no queue that draws and presents";
            continue;
        }
        int score = 0;
        switch (p.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
                score = 4;
                break;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
                score = 3;
                break;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
                score = 2;
                break;
            case VK_PHYSICAL_DEVICE_TYPE_CPU:
                score = 0;
                break;
            default:
                score = 1;
                break;
        }
        if (score <= bestScore) continue;
        bestScore = score;
        physical_ = pd;
        props_ = p;
        queueFamily_ = *family;
        deviceExtensions_ = std::move(exts);
        hasDynamicRendering_ = dynamicRendering;
        hasSync2_ = sync2;
        timestampMask_ =
            families[*family].timestampValidBits >= 64 ? ~0ull : ((1ull << families[*family].timestampValidBits) - 1);
        vkCaps_.timestamps = families[*family].timestampValidBits > 0 && p.limits.timestampPeriod > 0.f;
    }
    if (!physical_) return Result<void>::fail("vulkan: " + why);
    apiVersion_ = std::min(apiVersion_, std::min<std::uint32_t>(props_.apiVersion, VK_API_VERSION_1_3));
    return Result<void>::ok();
}

Result<void> VulkanDevice::createLogicalDevice() {
    const bool core13 = apiVersion_ >= VK_API_VERSION_1_3;
    // SH_VK_COMPAT=1 takes the render pass path on any device, to compare it with dynamic rendering.
    const char* compatEnv = std::getenv("SH_VK_COMPAT");
    const bool compat = compatEnv && compatEnv[0] && std::strcmp(compatEnv, "0") != 0;
    const bool useDynamic = hasDynamicRendering_ && !compat;
    const bool useSync2 = hasSync2_ && !compat;
    std::vector<const char*> exts;
    if (surface_) exts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    if (!core13) {
        if (useDynamic) exts.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        if (useSync2) exts.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
        // Dependencies of dynamic rendering on a 1.1 device.
        if (useDynamic && apiVersion_ < VK_API_VERSION_1_2) {
            if (hasExtension(deviceExtensions_, VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME))
                exts.push_back(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);
            if (hasExtension(deviceExtensions_, VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME))
                exts.push_back(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME);
        }
    }
    if (hasExtension(deviceExtensions_, kPortabilitySubset)) exts.push_back(kPortabilitySubset);
    memoryBudget_ = hasExtension(deviceExtensions_, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    if (memoryBudget_) exts.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    hdrMetadata_ =
        surface_ && vkCaps_.swapchainColorspace && hasExtension(deviceExtensions_, VK_EXT_HDR_METADATA_EXTENSION_NAME);
    if (hdrMetadata_) exts.push_back(VK_EXT_HDR_METADATA_EXTENSION_NAME);
    VkPhysicalDevicePresentIdFeaturesKHR fpid{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR};
    VkPhysicalDevicePresentWaitFeaturesKHR fpw{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR};
    if (surface_ && hasExtension(deviceExtensions_, VK_KHR_PRESENT_ID_EXTENSION_NAME) &&
        hasExtension(deviceExtensions_, VK_KHR_PRESENT_WAIT_EXTENSION_NAME)) {
        fpid.pNext = &fpw;
        VkPhysicalDeviceFeatures2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        query.pNext = &fpid;
        vkGetPhysicalDeviceFeatures2(physical_, &query);
        presentWait_ = fpid.presentId == VK_TRUE && fpw.presentWait == VK_TRUE;
        fpid.pNext = nullptr;
        fpw.pNext = nullptr;
        if (presentWait_) {
            exts.push_back(VK_KHR_PRESENT_ID_EXTENSION_NAME);
            exts.push_back(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
        }
    }

    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(physical_, &supported);
    fillModeNonSolid_ = supported.fillModeNonSolid == VK_TRUE;

    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceDynamicRenderingFeaturesKHR fdr{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
    VkPhysicalDeviceSynchronization2FeaturesKHR fs2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR};
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.features.fillModeNonSolid = supported.fillModeNonSolid;
    if (core13) {
        f13.dynamicRendering = useDynamic ? VK_TRUE : VK_FALSE;
        f13.synchronization2 = useSync2 ? VK_TRUE : VK_FALSE;
        if (useDynamic || useSync2) f2.pNext = &f13;
    } else {
        fdr.dynamicRendering = VK_TRUE;
        fs2.synchronization2 = VK_TRUE;
        if (useSync2) {
            fs2.pNext = f2.pNext;
            f2.pNext = &fs2;
        }
        if (useDynamic) {
            fdr.pNext = f2.pNext;
            f2.pNext = &fdr;
        }
    }
    VulkanRayTracingSetup rt;
    vulkanRayTracingChain(physical_, apiVersion_, deviceExtensions_, &rt, &exts, &f2.pNext);
    rayTracing_ = rt.supported;
    // Model upscaling (RENDER.md section 13): the tessellation feature and VK_EXT_mesh_shader, when there.
    tessellation_ = supported.tessellationShader == VK_TRUE;
    f2.features.tessellationShader = supported.tessellationShader;
    if (tessellation_)
        preRasterStages_ |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    VkPhysicalDeviceMeshShaderFeaturesEXT fmesh{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
    if (!hasExtension(deviceExtensions_, VK_EXT_MESH_SHADER_EXTENSION_NAME)) {
        meshShadersMissing_ = "no VK_EXT_mesh_shader";
    } else if (apiVersion_ < VK_API_VERSION_1_2) {
        meshShadersMissing_ = "mesh shaders need Vulkan 1.2";
    } else {
        VkPhysicalDeviceFeatures2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        query.pNext = &fmesh;
        vkGetPhysicalDeviceFeatures2(physical_, &query);
        if (fmesh.taskShader && fmesh.meshShader) {
            fmesh = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
            fmesh.taskShader = VK_TRUE;
            fmesh.meshShader = VK_TRUE;
            fmesh.pNext = f2.pNext;
            f2.pNext = &fmesh;
            exts.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
            meshShaders_ = true;
            preRasterStages_ |= VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT;
        } else {
            meshShadersMissing_ = "the device has no task and mesh shader stages";
        }
    }
    if (presentWait_) {
        fpw.pNext = f2.pNext;
        fpid.pNext = &fpw;
        f2.pNext = &fpid;
    }
    const float priority = 1.f;
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex = queueFamily_;
    q.queueCount = 1;
    q.pQueuePriorities = &priority;
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.pNext = &f2;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &q;
    info.enabledExtensionCount = static_cast<std::uint32_t>(exts.size());
    info.ppEnabledExtensionNames = exts.data();
    if (VkResult r = vkCreateDevice(physical_, &info, nullptr, &device_); r != VK_SUCCESS)
        return Result<void>::fail(vkError("vkCreateDevice", r));
    volkLoadDevice(device_);
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);

    // A driver that names the features but gives no entry points gets the Vulkan 1.0 path as well.
    passApi_.init(device_, core13, useDynamic, useSync2, kFrames);

    VmaVulkanFunctions functions{};
    VmaAllocatorCreateInfo a{};
    a.flags = memoryBudget_ ? VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT : 0;
    if (rayTracing_) a.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    a.physicalDevice = physical_;
    a.device = device_;
    a.instance = instance_;
    a.vulkanApiVersion = apiVersion_;
    if (VkResult r = vmaImportVulkanFunctionsFromVolk(&a, &functions); r != VK_SUCCESS)
        return Result<void>::fail(vkError("vmaImportVulkanFunctionsFromVolk", r));
    a.pVulkanFunctions = &functions;
    if (VkResult r = vmaCreateAllocator(&a, &allocator_); r != VK_SUCCESS)
        return Result<void>::fail(vkError("vmaCreateAllocator", r));

    loadPipelineStore();

    VkFormatProperties fp{};
    vkGetPhysicalDeviceFormatProperties(physical_, VK_FORMAT_D24_UNORM_S8_UINT, &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
        depthFormat_ = VK_FORMAT_D32_SFLOAT_S8_UINT;
    uboAlign_ = std::max<VkDeviceSize>(props_.limits.minUniformBufferOffsetAlignment, 16);
    uboRange_ = std::min<VkDeviceSize>(16384, props_.limits.maxUniformBufferRange);
    timestampPeriodNs_ = props_.limits.timestampPeriod;
    return Result<void>::ok();
}

Result<void> VulkanDevice::createFrames() {
    for (Frame& f : frames_) {
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = queueFamily_;
        if (VkResult r = vkCreateCommandPool(device_, &pool, nullptr, &f.pool); r != VK_SUCCESS)
            return Result<void>::fail(vkError("vkCreateCommandPool", r));
        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool = f.pool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 2;
        VkCommandBuffer cmds[2];
        if (VkResult r = vkAllocateCommandBuffers(device_, &alloc, cmds); r != VK_SUCCESS)
            return Result<void>::fail(vkError("vkAllocateCommandBuffers", r));
        f.cmd = cmds[0];
        f.preamble = cmds[1];
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(device_, &fence, nullptr, &f.fence);
        VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(device_, &sem, nullptr, &f.acquired);
        if (vkCaps_.timestamps) {
            VkQueryPoolCreateInfo qp{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            qp.queryType = VK_QUERY_TYPE_TIMESTAMP;
            qp.queryCount = kMaxQueries;
            vkCreateQueryPool(device_, &qp, nullptr, &f.queries);
        }
    }
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = queueFamily_;
    vkCreateCommandPool(device_, &pool, nullptr, &oneShotPool_);
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = oneShotPool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    vkAllocateCommandBuffers(device_, &alloc, &oneShot_);
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCreateFence(device_, &fence, nullptr, &oneShotFence_);
    return Result<void>::ok();
}

void VulkanDevice::fillCaps() {
    VulkanCaps& c = vkCaps_;
    c.apiVersion = apiVersion_;
    c.deviceApiVersion = props_.apiVersion;
    c.vendorId = props_.vendorID;
    c.deviceId = props_.deviceID;
    c.driverVersion = props_.driverVersion;
    c.deviceName = props_.deviceName;
    switch (props_.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            c.deviceType = "discrete";
            break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            c.deviceType = "integrated";
            break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            c.deviceType = "virtual";
            break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            c.deviceType = "cpu";
            break;
        default:
            c.deviceType = "other";
            break;
    }
    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    VkPhysicalDeviceSubgroupProperties subgroup{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    p2.pNext = &subgroup;
    if (apiVersion_ >= VK_API_VERSION_1_2) subgroup.pNext = &driver;
    vkGetPhysicalDeviceProperties2(physical_, &p2);
    c.subgroupSize = subgroup.subgroupSize;
    c.fragmentQuadOps = (subgroup.supportedStages & VK_SHADER_STAGE_FRAGMENT_BIT) != 0 &&
                        (subgroup.supportedOperations & VK_SUBGROUP_FEATURE_QUAD_BIT) != 0;
    if (apiVersion_ >= VK_API_VERSION_1_2) {
        c.driverName = driver.driverName;
        c.driverInfo = driver.driverInfo;
    }
    const auto& e = deviceExtensions_;
    c.accelerationStructure = hasExtension(e, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    c.rayQuery = hasExtension(e, VK_KHR_RAY_QUERY_EXTENSION_NAME);
    c.rayTracingPipeline = hasExtension(e, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    c.meshShader = hasExtension(e, VK_EXT_MESH_SHADER_EXTENSION_NAME);
    c.hdrMetadata = hasExtension(e, VK_EXT_HDR_METADATA_EXTENSION_NAME);
    c.memoryBudget = memoryBudget_;
    c.rayTracingEnabled = rayTracing_;
    c.dynamicRendering = passApi_.dynamicRendering();
    c.synchronization2 = passApi_.synchronization2();
    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(physical_, &features);
    c.tessellationShader = features.tessellationShader == VK_TRUE;

    const VkPhysicalDeviceMemoryProperties* mem = nullptr;
    vmaGetMemoryProperties(allocator_, &mem);
    std::vector<VmaBudget> budgets(mem->memoryHeapCount);
    vmaGetHeapBudgets(allocator_, budgets.data());
    for (std::uint32_t i = 0; i < mem->memoryHeapCount; ++i) {
        if (!(mem->memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)) continue;
        c.deviceLocalBytes += mem->memoryHeaps[i].size;
        if (memoryBudget_) c.vramBudgetBytes += budgets[i].budget;
    }

    caps_.driver = "vulkan " + versionText(apiVersion_) + " (native)";
    caps_.adapter = c.deviceName + (c.driverName.empty() ? std::string() : " / " + c.driverName + " " + c.driverInfo);
    // The SDL3 GPU conventions, so the same SPIR-V and the same callers serve both Vulkan backends.
    caps_.clipZeroToOne = true;
    caps_.fragCoordTopLeft = true;
    caps_.maxTextureSize = props_.limits.maxImageDimension2D;
    // Pipelines are not keyed on the sample count (as on SDL3 GPU): the scene renders single-sampled.
    caps_.maxSamples = 1;
    // Required by the spec for this format, checked anyway so a broken driver loses only the HDR frame.
    VkFormatProperties fp{};
    vkGetPhysicalDeviceFormatProperties(physical_, VK_FORMAT_R16G16B16A16_SFLOAT, &fp);
    const VkFormatFeatureFlags need =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
        VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
        VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    caps_.floatTargets = (fp.optimalTilingFeatures & need) == need;
    caps_.storageBuffers = true;
    caps_.tessellation = tessellation_;
    caps_.meshShaders = meshShaders_;
    caps_.fragmentQuadOps = c.fragmentQuadOps;
    caps_.meshShadersMissing = meshShaders_ ? std::string() : meshShadersMissing_;
}

Result<void> VulkanDevice::init() {
    if (auto r = createInstance(); !r) return r;
    if (auto r = pickPhysicalDevice(); !r) return r;
    if (auto r = createLogicalDevice(); !r) return r;
    if (auto r = createFrames(); !r) return r;
    fillCaps();
    // Or a file of that name in the working directory (a phone's game folder, where no environment is set).
    const char* plain = std::getenv("SH_VK_NO_MERGE");
    std::FILE* flag = std::fopen("SH_VK_NO_MERGE", "rb");
    if (flag) std::fclose(flag);
    if (flag || (plain && plain[0] != '\0' && plain[0] != '0')) {
        restructure_ = false;
        shader_log(0, "vulkan: SH_VK_NO_MERGE: one GPU pass per render pass, uploads where they are asked for");
    }

    TextureDesc d;
    d.width = d.height = 1;
    d.name = "vulkan dummy";
    auto dummy = createTexture(d);
    if (!dummy) return Result<void>::fail(dummy.error());
    dummy_ = dummy.value();
    VkSamplerCreateInfo s{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    s.magFilter = s.minFilter = VK_FILTER_NEAREST;
    s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(device_, &s, nullptr, &dummySampler_);
    if (surface_) {
        if (auto r = createSwapchain(); !r) return r;
    }
    startPrewarm();
    return Result<void>::ok();
}

// ---- swapchain --------------------------------------------------------------------------------------------------

void VulkanDevice::destroySwapchain() {
    for (TextureH h : swapTextures_) {
        if (auto t = textures_.remove(h)) {
            passApi_.forget(t->view);
            vkDestroyImageView(device_, t->view, nullptr);
        }
    }
    swapTextures_.clear();
    for (VkSemaphore s : renderDone_) vkDestroySemaphore(device_, s, nullptr);
    renderDone_.clear();
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}

Result<void> VulkanDevice::createSwapchain() {
    VkSurfaceCapabilitiesKHR sc{};
    if (VkResult r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &sc); r != VK_SUCCESS)
        return markLost("surface capabilities", r);
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(desc_.window, &pw, &ph);
    VkExtent2D extent = sc.currentExtent;
    if (extent.width == 0xFFFFFFFFu) {
        extent.width = std::clamp<std::uint32_t>(static_cast<std::uint32_t>(std::max(pw, 0)), sc.minImageExtent.width,
                                                 sc.maxImageExtent.width);
        extent.height = std::clamp<std::uint32_t>(static_cast<std::uint32_t>(std::max(ph, 0)), sc.minImageExtent.height,
                                                  sc.maxImageExtent.height);
    }
    vkDeviceWaitIdle(device_);
    completed_ = submitted_;
    VkSwapchainKHR old = swapchain_;
    swapchain_ = VK_NULL_HANDLE;
    for (TextureH h : swapTextures_) {
        if (auto t = textures_.remove(h)) {
            passApi_.forget(t->view);
            vkDestroyImageView(device_, t->view, nullptr);
        }
    }
    swapTextures_.clear();
    for (VkSemaphore s : renderDone_) vkDestroySemaphore(device_, s, nullptr);
    renderDone_.clear();
    swapchainDirty_ = false;
    if (extent.width == 0 || extent.height == 0) {  // minimised: made again when the window comes back
        if (old) vkDestroySwapchainKHR(device_, old, nullptr);
        swapExtent_ = {};
        return Result<void>::ok();
    }

    std::uint32_t n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(n);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &n, formats.data());
    // SDR as the SDL3 GPU backend presents it: 8-bit UNORM, no sRGB encode on write.
    VkSurfaceFormatKHR chosen =
        formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR} : formats[0];
    for (const VkSurfaceFormatKHR& f : formats) {
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }
    // HDR10: PQ-encoded BT.2020 in a 10-bit format; the engine writes the encoded values itself (hdr_pass.cpp).
    hdrActive_ = false;
    if (!hdr_.enabled) {
        hdrBlock_ = VulkanHdrBlock::Off;
    } else if (!vkCaps_.swapchainColorspace) {
        hdrBlock_ = VulkanHdrBlock::NoColorspaceExtension;
    } else {
        hdrBlock_ = VulkanHdrBlock::NoHdr10Format;
        for (const VkSurfaceFormatKHR& f : formats) {
            if ((f.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 || f.format == VK_FORMAT_A2R10G10B10_UNORM_PACK32) &&
                f.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT) {
                chosen = f;
                hdrActive_ = true;
                hdrBlock_ = VulkanHdrBlock::None;
                break;
            }
        }
    }
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &n, nullptr);
    std::vector<VkPresentModeKHR> modes(n);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &n, modes.data());
    auto has = [&](VkPresentModeKHR m) { return std::find(modes.begin(), modes.end(), m) != modes.end(); };
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    switch (pacing_.presentMode) {
        case 1:
            break;
        case 2:
            if (has(VK_PRESENT_MODE_MAILBOX_KHR)) mode = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        case 3:
            if (has(VK_PRESENT_MODE_IMMEDIATE_KHR)) mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            break;
        case 4:
            if (has(VK_PRESENT_MODE_FIFO_RELAXED_KHR)) mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
            break;
        default:
            if (!desc_.vsync) {
                if (has(VK_PRESENT_MODE_IMMEDIATE_KHR))
                    mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
                else if (has(VK_PRESENT_MODE_MAILBOX_KHR))
                    mode = VK_PRESENT_MODE_MAILBOX_KHR;
            }
            break;
    }
    // Present ids keep rising across swapchains; a wait only ever names an id of the current one.
    shownFrom_ = presentId_;
    std::uint32_t images = std::max<std::uint32_t>(sc.minImageCount + 1, 3);
    if (sc.maxImageCount > 0) images = std::min(images, sc.maxImageCount);
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(sc.supportedCompositeAlpha & alpha)) {
        for (VkCompositeAlphaFlagBitsKHR a :
             {VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
              VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR}) {
            if (sc.supportedCompositeAlpha & a) {
                alpha = a;
                break;
            }
        }
    }
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface_;
    info.minImageCount = images;
    info.imageFormat = chosen.format;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      (sc.supportedUsageFlags & (VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = (sc.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                            ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                            : sc.currentTransform;
    info.compositeAlpha = alpha;
    info.presentMode = mode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = old;
    VkResult r = vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_);
    if (old) vkDestroySwapchainKHR(device_, old, nullptr);
    if (r != VK_SUCCESS) {
        swapchain_ = VK_NULL_HANDLE;
        return markLost("vkCreateSwapchainKHR", r);
    }
    swapFormat_ = chosen.format;
    swapExtent_ = extent;
    swapRotated_ = info.preTransform != sc.currentTransform;
    {
        auto modeName = [](VkPresentModeKHR m) {
            switch (m) {
                case VK_PRESENT_MODE_IMMEDIATE_KHR: return "immediate";
                case VK_PRESENT_MODE_MAILBOX_KHR: return "mailbox";
                case VK_PRESENT_MODE_FIFO_KHR: return "fifo";
                case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "fifo relaxed";
                default: return "other";
            }
        };
        std::uint32_t made = 0;
        vkGetSwapchainImagesKHR(device_, swapchain_, &made, nullptr);
        char line[256];
        std::snprintf(line, sizeof line,
                      "[PERF] swapchain %ux%u, %u images (asked %u, surface min %u), present %s, transform 0x%x "
                      "(surface 0x%x), format %d, %u frames in flight",
                      extent.width, extent.height, made, images, sc.minImageCount, modeName(mode),
                      static_cast<unsigned>(info.preTransform), static_cast<unsigned>(sc.currentTransform),
                      static_cast<int>(chosen.format), pacing_.framesInFlight <= 1 ? 1u : kFrames);
        shader_log(0, line);
    }
    if (hdrActive_ && hdrMetadata_) applyHdrMetadata();
    if (hdr_.enabled)
        shader_log(hdrActive_ ? 0 : 1,
                   std::string("vulkan: HDR10 output ") +
                       (hdrActive_ ? (hdrMetadata_ ? "on, with HDR metadata" : "on, no HDR metadata")
                                   : std::string("unavailable: ") + hdrBlockText(hdrBlock_)));
    vkGetSwapchainImagesKHR(device_, swapchain_, &n, nullptr);
    std::vector<VkImage> list(n);
    vkGetSwapchainImagesKHR(device_, swapchain_, &n, list.data());
    for (VkImage image : list) {
        VkTexture t;
        t.image = image;
        t.format = chosen.format;
        t.swapchain = true;
        t.desc.width = extent.width;
        t.desc.height = extent.height;
        t.desc.usage = kUsageColorTarget;
        t.rest = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        v.image = image;
        v.viewType = VK_IMAGE_VIEW_TYPE_2D;
        v.format = chosen.format;
        v.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(device_, &v, nullptr, &t.view);
        t.sampledView = t.view;
        swapTextures_.push_back(textures_.add(t));
        VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore s = VK_NULL_HANDLE;
        vkCreateSemaphore(device_, &sem, nullptr, &s);
        renderDone_.push_back(s);
    }
    return Result<void>::ok();
}

// ---- resources --------------------------------------------------------------------------------------------------

void VulkanDevice::retire(std::function<void()> release) {
    // Anything recorded so far may still use the object: the open list will be submission submitted_ + 1.
    garbage_.push_back({recording_ ? submitted_ + 1 : submitted_, std::move(release)});
}

void VulkanDevice::collectGarbage() {
    while (!garbage_.empty() && garbage_.front().serial <= completed_) {
        garbage_.front().release();
        garbage_.pop_front();
    }
}

void VulkanDevice::releaseTexture(VkTexture& t) {
    if (t.swapchain || t.external) return;
    // Made and released outside a frame, it may be freed before the next submission: its first transition must not
    // be recorded on a freed image. Inside a frame the open command list may already use it (a target remade when a
    // pass first asks for sampled depth), and retire() keeps it alive until that list completes, so it keeps its
    // transition out of UNDEFINED.
    if (!recording_) std::erase_if(pendingInits_, [&](const PendingInit& p) { return p.image == t.image; });
    VkDevice dev = device_;
    VmaAllocator allocator = allocator_;
    const VkImage image = t.image;
    const VkImageView view = t.view, sampled = t.sampledView;
    const VmaAllocation alloc = t.alloc;
    passApi_.forget(view);
    retire([dev, allocator, image, view, sampled, alloc] {
        if (sampled != view) vkDestroyImageView(dev, sampled, nullptr);
        vkDestroyImageView(dev, view, nullptr);
        vmaDestroyImage(allocator, image, alloc);
    });
}

Result<TextureH> VulkanDevice::makeImage(const TextureDesc& desc, VkFormat format, VkImageUsageFlags usage) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {desc.width, desc.height, 1};
    // A chain only for a sampled colour texture: attachments keep one level, their views must have one.
    std::uint32_t levels = 1;
    if (desc.mipLevels > 1 && desc.format != TextureFormat::D24S8 &&
        !(desc.usage & (kUsageColorTarget | kUsageDepthStencilTarget))) {
        for (std::uint32_t side = std::max(desc.width, desc.height); side > 1 && levels < desc.mipLevels; side /= 2)
            ++levels;
    }
    info.mipLevels = levels;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    VkTexture t;
    t.format = format;
    t.desc = desc;
    t.desc.name = {};
    t.desc.mipLevels = levels;
    const bool depth = desc.format == TextureFormat::D24S8;
    t.aspect = depth ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) : VK_IMAGE_ASPECT_COLOR_BIT;
    if (desc.usage & kUsageSampled)
        t.rest = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    else if (depth)
        t.rest = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    else
        t.rest = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VmaAllocationInfo out{};
    if (VkResult r = vmaCreateImage(allocator_, &info, &ai, &t.image, &t.alloc, &out); r != VK_SUCCESS)
        return Result<TextureH>::fail(vkError("vmaCreateImage", r));
    VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    v.image = t.image;
    v.viewType = VK_IMAGE_VIEW_TYPE_2D;
    v.format = format;
    v.subresourceRange = {t.aspect, 0, levels, 0, 1};
    vkCreateImageView(device_, &v, nullptr, &t.view);
    t.sampledView = t.view;
    if (depth && (desc.usage & kUsageSampled)) {
        v.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        vkCreateImageView(device_, &v, nullptr, &t.sampledView);
    }
    if (!desc.name.empty() && vkSetDebugUtilsObjectNameEXT && vkCaps_.validation) {
        const std::string name(desc.name);
        VkDebugUtilsObjectNameInfoEXT n{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
        n.objectType = VK_OBJECT_TYPE_IMAGE;
        n.objectHandle = reinterpret_cast<std::uint64_t>(t.image);
        n.pObjectName = name.c_str();
        vkSetDebugUtilsObjectNameEXT(device_, &n);
    }
    t.bytes = out.size;
    appVramBytes_ += t.bytes;
    // The first use finds the texture in its resting layout: the transition from UNDEFINED is recorded ahead of the
    // next submission (or the next readback).
    pendingInits_.push_back({t.image, t.aspect, t.rest});
    t.layout = t.rest;
    return Result<TextureH>::ok(textures_.add(t));
}

Result<TextureH> VulkanDevice::createTexture(const TextureDesc& desc) {
    if (desc.samples > 1) return Result<TextureH>::fail("vulkan: multisampled targets are not supported");
    if (desc.width == 0 || desc.height == 0) return Result<TextureH>::fail("vulkan: texture of size 0");
    VkImageUsageFlags usage = 0;
    if (desc.format == TextureFormat::D24S8) {
        if (desc.usage & kUsageDepthStencilTarget) usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (desc.usage & kUsageSampled) usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    } else {
        usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (desc.usage & kUsageSampled) usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        if (desc.usage & kUsageColorTarget) usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    return makeImage(desc, textureFormat(desc.format), usage);
}

Result<BufferH> VulkanDevice::createBuffer(const BufferDesc& desc) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = std::max<std::uint32_t>(desc.size, 4);
    info.usage =
        (desc.usage == BufferUsage::Index ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT : VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) |
        (desc.shaderRead ? VkBufferUsageFlags{VK_BUFFER_USAGE_STORAGE_BUFFER_BIT} : 0u) |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    // Ray tracing builds its acceleration structures from the scene's own buffers and reads them in its shaders.
    if (rayTracing_)
        info.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    VkBuf b;
    if (VkResult r = vmaCreateBuffer(allocator_, &info, &ai, &b.buffer, &b.alloc, nullptr); r != VK_SUCCESS)
        return Result<BufferH>::fail(vkError("vmaCreateBuffer", r));
    b.desc = desc;
    b.desc.name = {};
    appVramBytes_ += desc.size;
    return Result<BufferH>::ok(buffers_.add(b));
}

Result<SamplerH> VulkanDevice::createSampler(const SamplerDesc& desc) {
    auto address = [](AddressMode m) {
        switch (m) {
            case AddressMode::ClampToEdge:
                return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case AddressMode::Repeat:
                return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            case AddressMode::MirroredRepeat:
                return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        }
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    };
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.minFilter = desc.minFilter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.magFilter = desc.magFilter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = address(desc.addressU);
    info.addressModeV = address(desc.addressV);
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.minLod = 0.f;
    info.maxLod = 0.f;
    VkSamp s;
    if (VkResult r = vkCreateSampler(device_, &info, nullptr, &s.sampler); r != VK_SUCCESS)
        return Result<SamplerH>::fail(vkError("vkCreateSampler", r));
    return Result<SamplerH>::ok(samplers_.add(s));
}

Result<ShaderH> VulkanDevice::createShader(const ShaderDesc& desc) {
    if (desc.spirv.empty()) return Result<ShaderH>::fail("vulkan: shader " + std::string(desc.name) + " has no SPIR-V");
    const bool tessStage = desc.stage == ShaderStage::TessControl || desc.stage == ShaderStage::TessEval;
    const bool meshStage = desc.stage == ShaderStage::Task || desc.stage == ShaderStage::Mesh;
    if ((tessStage && !tessellation_) || (meshStage && !meshShaders_))
        return Result<ShaderH>::fail("vulkan: shader " + std::string(desc.name) + ": the device lacks its stage");
    if (desc.samplerNames.size() > kMaxSamplerSlots || desc.uniformBlockNames.size() > kMaxUniformSlots ||
        desc.storageBuffers > kMaxStorageSlots)
        return Result<ShaderH>::fail("vulkan: shader " + std::string(desc.name) + " has too many resource slots");
    auto make = [&](const void* code, std::size_t bytes, VkShaderModule* out) {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = bytes;
        info.pCode = static_cast<const std::uint32_t*>(code);
        return vkCreateShaderModule(device_, &info, nullptr, out);
    };
    VkShaderModule module = VK_NULL_HANDLE;
    std::vector<std::uint32_t> used;
    // A mod's SPIR-V (shaders/<file>.spv), checked as the SDL3 GPU backend checks it.
    if (const std::optional<std::string> mod = shader_override(desc.name, ".spv"); mod) {
        const std::string where = "shaders/" + shader_file_name(desc.name) + ".spv";
        std::uint32_t magic = 0;
        if (mod->size() >= 20 && mod->size() % 4 == 0) std::memcpy(&magic, mod->data(), sizeof magic);
        if (magic != 0x07230203u) {
            shader_log(1, where + " is not SPIR-V, the built-in shader is used");
        } else {
            std::vector<std::uint32_t> words(mod->size() / 4);
            std::memcpy(words.data(), mod->data(), mod->size());
            if (VkResult r = make(words.data(), mod->size(), &module); r == VK_SUCCESS) {
                used = std::move(words);
                shader_log(0, "shader " + std::string(desc.name) + ": the mod's " + where);
            } else {
                module = VK_NULL_HANDLE;
                shader_log(1, where + ": " + vkError("vkCreateShaderModule", r) + ", the built-in shader is used");
            }
        }
    }
    if (!module) {
        if (VkResult r = make(desc.spirv.data(), desc.spirv.size_bytes(), &module); r != VK_SUCCESS)
            return Result<ShaderH>::fail(vkError(("shader " + std::string(desc.name)).c_str(), r));
        used.assign(desc.spirv.begin(), desc.spirv.end());
    }
    VkShader s;
    VkDevice dev = device_;
    const std::uint64_t hash = pipeline_store::hash64(std::span<const std::uint32_t>(used));
    auto code = std::make_shared<const std::vector<std::uint32_t>>(std::move(used));
    s.module =
        std::shared_ptr<ShaderModule>(new ShaderModule{module, hash, std::move(code)}, [this, dev](ShaderModule* m) {
            const VkShaderModule handle = m->module;
            delete m;
            retire([dev, handle] { vkDestroyShaderModule(dev, handle, nullptr); });
        });
    s.stage = desc.stage;
    s.samplers = static_cast<std::uint32_t>(desc.samplerNames.size());
    s.uniforms = static_cast<std::uint32_t>(desc.uniformBlockNames.size());
    s.storage = desc.storageBuffers;
    s.entry = std::string(desc.entryPoint);
    return Result<ShaderH>::ok(shaders_.add(std::move(s)));
}

VkDescriptorSetLayout VulkanDevice::setLayout(SetKind kind, std::uint32_t count) {
    const std::uint64_t key = (std::uint64_t{kind} << 32) | count;
    if (auto it = setLayouts_.find(key); it != setLayouts_.end()) return it->second;
    const bool sampler = kind == kVertexSamplers || kind == kFragmentSamplers;
    const VkShaderStageFlags stage = (kind == kFragmentSamplers || kind == kFragmentUniforms)
                                         ? VkShaderStageFlags{VK_SHADER_STAGE_FRAGMENT_BIT}
                                         : preRasterStages_;
    std::vector<VkDescriptorSetLayoutBinding> bindings(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = kind == kStorageBuffers ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                                     : sampler               ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
                                                             : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = stage;
    }
    VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.bindingCount = count;
    info.pBindings = bindings.data();
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    vkCreateDescriptorSetLayout(device_, &info, nullptr, &layout);
    setLayouts_[key] = layout;
    return layout;
}

VkPipelineLayout VulkanDevice::pipelineLayout(const std::array<std::uint32_t, kSetKinds>& counts,
                                              const std::array<VkDescriptorSetLayout, kSetKinds>& layouts) {
    const std::uint64_t key = (std::uint64_t{counts[4]} << 32) | (std::uint64_t{counts[0]} << 24) | (counts[1] << 16) |
                              (counts[2] << 8) | counts[3];
    if (auto it = pipelineLayouts_.find(key); it != pipelineLayouts_.end()) return it->second;
    VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    // Set 4 only where a stage reads storage buffers, so the other layouts stay as on SDL3 GPU.
    info.setLayoutCount = counts[4] > 0 ? kSetKinds : 4;
    info.pSetLayouts = layouts.data();
    VkPipelineLayout layout = VK_NULL_HANDLE;
    vkCreatePipelineLayout(device_, &info, nullptr, &layout);
    pipelineLayouts_[key] = layout;
    return layout;
}

Result<VkPipe> VulkanDevice::makePipe(const PipelineDesc& desc) {
    VkShader* mesh = desc.meshShader ? shaders_.get(desc.meshShader) : nullptr;
    VkShader* vs = mesh ? mesh : shaders_.get(desc.vertexShader);
    VkShader* fs = shaders_.get(desc.fragmentShader);
    if (!vs || !fs) return Result<VkPipe>::fail("vulkan: pipeline with a released shader");
    VkShader* task = mesh && desc.taskShader ? shaders_.get(desc.taskShader) : nullptr;
    VkShader* tcs = !mesh && desc.tessControlShader ? shaders_.get(desc.tessControlShader) : nullptr;
    VkShader* tes = !mesh && desc.tessEvalShader ? shaders_.get(desc.tessEvalShader) : nullptr;
    if ((mesh && desc.taskShader && !task) ||
        (!mesh && (desc.tessControlShader || desc.tessEvalShader) && (!tcs || !tes)))
        return Result<VkPipe>::fail("vulkan: pipeline with a released shader");
    VkPipe p;
    p.desc = desc;
    p.attributes.assign(desc.vertexLayout.attributes.begin(), desc.vertexLayout.attributes.end());
    p.desc.vertexLayout.attributes = {};
    p.name = std::string(desc.name);
    p.desc.name = {};
    p.vs = vs->module;
    p.fs = fs->module;
    p.vsEntry = vs->entry;
    p.fsEntry = fs->entry;
    p.counts = {vs->samplers, vs->uniforms, fs->samplers, fs->uniforms, vs->storage};
    // The stages before rasterisation share sets 0, 1 and 4: each set is as large as the largest use.
    for (VkShader* extra : {task, tcs, tes}) {
        if (!extra) continue;
        p.counts[kVertexSamplers] = std::max(p.counts[kVertexSamplers], extra->samplers);
        p.counts[kVertexUniforms] = std::max(p.counts[kVertexUniforms], extra->uniforms);
        p.counts[kStorageBuffers] = std::max(p.counts[kStorageBuffers], extra->storage);
    }
    if (mesh) {
        p.mesh = mesh->module;
        p.meshEntry = mesh->entry;
        p.vs.reset();
        if (task) {
            p.task = task->module;
            p.taskEntry = task->entry;
        }
    }
    if (tcs && tes) {
        p.tcs = tcs->module;
        p.tes = tes->module;
        p.tcsEntry = tcs->entry;
        p.tesEntry = tes->entry;
    }
    for (std::uint32_t k = 0; k < kSetKinds; ++k) p.setLayouts[k] = setLayout(static_cast<SetKind>(k), p.counts[k]);
    p.layout = pipelineLayout(p.counts, p.setLayouts);
    return Result<VkPipe>::ok(std::move(p));
}

Result<PipelineH> VulkanDevice::createPipeline(const PipelineDesc& desc) {
    auto made = makePipe(desc);
    if (!made) return Result<PipelineH>::fail(made.error());
    VkPipe& p = made.value();
    // Bake the declared formats now: an error shows at creation, as on the other backends.
    const VkFormat color = textureFormat(desc.colorFormat);
    const VkFormat depth = desc.hasDepthStencil ? textureFormat(desc.depthStencilFormat) : VK_FORMAT_UNDEFINED;
    if (!variant(p, color, depth)) return Result<PipelineH>::fail("vulkan: pipeline " + p.name + " failed");
    return Result<PipelineH>::ok(pipelines_.add(std::move(p)));
}

vkn::GraphicsRecipe VulkanDevice::recipeFor(const VkPipe& p, VkFormat color, VkFormat depth) const {
    using R = vkn::GraphicsRecipe;
    R r;
    auto stage = [&](R::Stage st, const std::shared_ptr<ShaderModule>& m, const std::string& entry) {
        if (!m) return;
        r.shader[st] = m->hash;
        r.entry[st] = entry;
    };
    stage(R::kTask, p.task, p.taskEntry);
    stage(R::kMesh, p.mesh, p.meshEntry);
    stage(R::kVertex, p.vs, p.vsEntry);
    stage(R::kTessControl, p.tcs, p.tcsEntry);
    stage(R::kTessEval, p.tes, p.tesEntry);
    stage(R::kFragment, p.fs, p.fsEntry);
    r.sets = p.counts;
    r.stride = p.desc.vertexLayout.stride;
    for (const VertexAttribute& a : p.attributes)
        r.attributes.push_back({a.location, vkVertexFormat(a.format), a.offset});
    switch (p.desc.primitive) {
        case PrimitiveType::TriangleList:
            r.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            break;
        case PrimitiveType::TriangleStrip:
            r.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            break;
        case PrimitiveType::LineList:
            r.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            break;
    }
    r.patchControlPoints = p.desc.patchControlPoints;
    r.wireframe = p.desc.wireframe;
    r.cull = p.desc.cull == CullMode::None    ? VK_CULL_MODE_NONE
             : p.desc.cull == CullMode::Front ? VK_CULL_MODE_FRONT_BIT
                                              : VK_CULL_MODE_BACK_BIT;
    // With the flipped viewport (setViewport) the winding reads as in GL, exactly as on SDL3 GPU.
    r.frontFace = p.desc.frontFace == FrontFace::Clockwise ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    const DepthStencilState& s = p.desc.depthStencil;
    const bool hasDepth = depth != VK_FORMAT_UNDEFINED;
    r.depthTest = hasDepth && s.depthTest;
    r.depthWrite = hasDepth && s.depthTest && s.depthWrite;
    r.depthCompare = vkCompare(s.depthCompare);
    r.stencilTest = hasDepth && hasStencil(depth) && s.stencilTest;
    r.stencil = {vkStencilOp(s.stencil.fail),
                 vkStencilOp(s.stencil.pass),
                 vkStencilOp(s.stencil.depthFail),
                 vkCompare(s.stencil.compare),
                 s.stencilReadMask,
                 s.stencilWriteMask,
                 0};
    const BlendState& b = p.desc.blend;
    r.blend = b.enable;
    r.srcColor = vkBlendFactor(b.srcColor);
    r.dstColor = vkBlendFactor(b.dstColor);
    r.colorOp = vkBlendOp(b.colorOp);
    r.srcAlpha = vkBlendFactor(b.srcAlpha);
    r.dstAlpha = vkBlendFactor(b.dstAlpha);
    r.alphaOp = vkBlendOp(b.alphaOp);
    r.writeMask = b.writeMask & 0xF;
    r.color = color;
    r.depth = depth;
    return r;
}

vkn::BuildTarget VulkanDevice::buildTarget(const vkn::GraphicsRecipe& recipe) {
    std::array<VkDescriptorSetLayout, kSetKinds> layouts{};
    for (std::uint32_t k = 0; k < kSetKinds; ++k) layouts[k] = setLayout(static_cast<SetKind>(k), recipe.sets[k]);
    vkn::BuildTarget t;
    t.layout = pipelineLayout(recipe.sets, layouts);
    t.renderPass = passApi_.compatiblePass(recipe.color, recipe.depth);
    return t;
}

VkPipeline VulkanDevice::variant(VkPipe& p, VkFormat color, VkFormat depth) {
    for (const VkPipe::Variant& v : p.variants)
        if (v.color == color && v.depth == depth) return v.pipeline;

    const vkn::GraphicsRecipe recipe = recipeFor(p, color, depth);
    std::vector<std::uint8_t> bytes = recipe.encode();
    const std::uint64_t key = pipeline_store::hash64(bytes);
    // Two callers asking for the same pipeline share one.
    if (auto it = shared_.find(key); it != shared_.end()) {
        ++it->second.refs;
        p.variants.push_back({color, depth, it->second.pipeline, key});
        return it->second.pipeline;
    }
    double waited = 0.0;
    VkPipeline pipeline = warmer_.take(key, &waited);
    if (pipeline) {
        ++buildLog_.taken;
        if (waited > 0.0) {
            pipelineBuildMs_ += waited;
            buildLog_.ms += waited;
        }
    } else {
        using R = vkn::GraphicsRecipe;
        const std::array<VkShaderModule, R::kStages> modules{
            p.task ? p.task->module : VK_NULL_HANDLE, p.mesh ? p.mesh->module : VK_NULL_HANDLE,
            p.vs ? p.vs->module : VK_NULL_HANDLE,     p.tcs ? p.tcs->module : VK_NULL_HANDLE,
            p.tes ? p.tes->module : VK_NULL_HANDLE,   p.fs ? p.fs->module : VK_NULL_HANDLE};
        vkn::BuildTarget target;
        target.layout = p.layout;
        target.renderPass = passApi_.compatiblePass(color, depth);
        const Uint64 buildStart = SDL_GetPerformanceCounter();
        const VkResult built =
            vkn::buildGraphicsPipeline(device_, pipelineCache_, recipe, modules, target, fillModeNonSolid_, &pipeline);
        const double ms = static_cast<double>(SDL_GetPerformanceCounter() - buildStart) * 1000.0 /
                          static_cast<double>(SDL_GetPerformanceFrequency());
        ++pipelinesBuilt_;
        pipelineBuildMs_ += ms;
        ++buildLog_.built;
        buildLog_.ms += ms;
        if (ms > buildLog_.longestMs) {
            buildLog_.longestMs = ms;
            buildLog_.longest = p.name;
        }
        if (built != VK_SUCCESS) {
            shader_log(1, "pipeline " + p.name + ": " + vkError("vkCreateGraphicsPipelines", built));
            return VK_NULL_HANDLE;
        }
    }
    notePipeline(key, std::move(bytes), recipe, p);
    shared_[key] = {pipeline, 1};
    p.variants.push_back({color, depth, pipeline, key});
    return pipeline;
}

// ---- pipeline cache on disk and prewarm (RENDER.md section 2.2d) ------------------------------------------------

namespace {
constexpr const char* kCacheFile = "vulkan.cache";
constexpr const char* kManifestFile = "vulkan.manifest";
constexpr Uint64 kStoreQuietMs = 5000;  // a save waits this long after the last new pipeline
constexpr Uint64 kBuildLogMs = 5000;

std::array<vkn::SpirvCode, vkn::GraphicsRecipe::kStages> stageCode(const VkPipe& p) {
    using R = vkn::GraphicsRecipe;
    std::array<vkn::SpirvCode, R::kStages> code{};
    auto put = [&](R::Stage st, const std::shared_ptr<ShaderModule>& m) {
        if (m) code[st] = m->code;
    };
    put(R::kTask, p.task);
    put(R::kMesh, p.mesh);
    put(R::kVertex, p.vs);
    put(R::kTessControl, p.tcs);
    put(R::kTessEval, p.tes);
    put(R::kFragment, p.fs);
    return code;
}
}  // namespace

void VulkanDevice::loadPipelineStore() {
    storeId_.game_version = desc_.gameVersion;
    storeId_.vendor_id = props_.vendorID;
    storeId_.device_id = props_.deviceID;
    storeId_.driver_version = props_.driverVersion;
    std::memcpy(storeId_.cache_uuid.data(), props_.pipelineCacheUUID, storeId_.cache_uuid.size());
    std::vector<std::uint8_t> initial;
    std::string state = "in memory only";
    if (!desc_.pipelineCacheDir.empty()) {
        storeDir_ = std::filesystem::path(desc_.pipelineCacheDir);
        if (auto file = pipeline_store::read_file(storeDir_ / kCacheFile, pipeline_store::kMaxCacheBytes)) {
            if (auto data = pipeline_store::unpack_cache(*file, storeId_)) {
                initial = std::move(*data);
                cacheValid_ = true;
                state = std::to_string(initial.size() / 1024) + " KB read";
            } else {
                state = "discarded (another build of the game, another driver or GPU, or a damaged file)";
            }
        } else {
            state = "none yet";
        }
        if (auto file = pipeline_store::read_file(storeDir_ / kManifestFile, pipeline_store::kMaxManifestBytes)) {
            if (auto m = pipeline_store::unpack_manifest(*file)) earlier_ = std::move(*m);
        }
    }
    VkPipelineCacheCreateInfo pc{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    pc.initialDataSize = initial.size();
    pc.pInitialData = initial.empty() ? nullptr : initial.data();
    if (vkCreatePipelineCache(device_, &pc, nullptr, &pipelineCache_) != VK_SUCCESS && !initial.empty()) {
        cacheValid_ = false;
        state = "refused by the driver";
        pc.initialDataSize = 0;
        pc.pInitialData = nullptr;
        vkCreatePipelineCache(device_, &pc, nullptr, &pipelineCache_);
    }
    shader_log(0, "[GPU] pipeline cache: " + state + (storeDir_.empty() ? "" : " (" + storeDir_.string() + ")") +
                      "; manifest: " + std::to_string(earlier_.recipes.size()) + " pipelines");
}

void VulkanDevice::startPrewarm() {
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    // Without the prewarm one worker remains, for the saves.
    const unsigned threads = desc_.pipelinePrewarm ? std::clamp(hw / 2, 1u, 6u) : 1u;
    warmer_.start(device_, pipelineCache_, fillModeNonSolid_, threads);
    storeReady_ = true;
    prewarmStartMs_ = SDL_GetTicks();
    buildLogMs_ = prewarmStartMs_;
    if (!desc_.pipelinePrewarm) {
        shader_log(0, "[GPU] pipeline prewarm: off (shader_prewarm = 0)");
        return;
    }
    using R = vkn::GraphicsRecipe;
    auto attachable = [&](VkFormat f, VkFormatFeatureFlags need) {
        if (f == VK_FORMAT_UNDEFINED) return true;
        VkFormatProperties fp{};
        vkGetPhysicalDeviceFormatProperties(physical_, f, &fp);
        return (fp.optimalTilingFeatures & need) == need;
    };
    std::unordered_map<std::uint64_t, vkn::SpirvCode> code;
    for (const auto& [hash, words] : earlier_.shaders)
        code.emplace(hash, std::make_shared<const std::vector<std::uint32_t>>(words));
    for (const pipeline_store::Recipe& entry : earlier_.recipes) {
        const std::optional<R> recipe = R::decode(entry.bytes);
        if (!recipe) continue;
        if ((recipe->shader[R::kTessControl] || recipe->shader[R::kTessEval]) && !tessellation_) continue;
        if ((recipe->shader[R::kTask] || recipe->shader[R::kMesh]) && !meshShaders_) continue;
        if (!attachable(recipe->color, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) ||
            !attachable(recipe->depth, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
            continue;
        vkn::PipelineWarmer::Job job;
        job.key = entry.key();
        job.recipe = *recipe;
        bool complete = true;
        for (std::uint32_t st = 0; st < R::kStages; ++st) {
            if (!recipe->shader[st]) continue;
            const auto it = code.find(recipe->shader[st]);
            if (it == code.end())
                complete = false;
            else
                job.code[st] = it->second;
        }
        if (!complete) continue;
        job.target = buildTarget(*recipe);
        if (warmer_.add(std::move(job))) ++prewarmQueued_;
    }
    shader_log(0, "[GPU] pipeline prewarm: " + std::to_string(prewarmQueued_) + " pipelines of the manifest on " +
                      std::to_string(threads) + " threads" + (cacheValid_ ? "" : " (no valid cache: full builds)"));
}

void VulkanDevice::notePipeline(std::uint64_t key, std::vector<std::uint8_t> bytes, const vkn::GraphicsRecipe& recipe,
                                const VkPipe& p) {
    if (storeDir_.empty()) return;
    std::lock_guard<std::mutex> lock(storeMutex_);
    if (!createdKeys_.insert(key).second) return;
    pipeline_store::Recipe entry;
    entry.bytes = std::move(bytes);
    entry.shaders = recipe.shaders();
    for (const std::shared_ptr<ShaderModule>* m : {&p.task, &p.mesh, &p.vs, &p.tcs, &p.tes, &p.fs}) {
        if (*m && (*m)->code && !created_.shaders.contains((*m)->hash))
            created_.shaders.emplace((*m)->hash, *(*m)->code);
    }
    created_.recipes.push_back(std::move(entry));
    storeDirty_ = true;
    lastNewPipelineMs_ = SDL_GetTicks();
}

void VulkanDevice::savePipelineStore(bool wait) {
    if (storeDir_.empty() || !pipelineCache_ || !storeReady_) return;
    if (!wait && (saving_.exchange(true) || !warmer_.running())) {
        if (!warmer_.running()) saving_ = false;
        return;
    }
    pipeline_store::Manifest snapshot;
    {
        std::lock_guard<std::mutex> lock(storeMutex_);
        snapshot = created_;
        storeDirty_ = false;
    }
    auto work = [this, snapshot = std::move(snapshot), wait]() {
        std::lock_guard<std::mutex> lock(saveMutex_);
        std::size_t size = 0;
        bool cacheOk = false;
        if (vkGetPipelineCacheData(device_, pipelineCache_, &size, nullptr) == VK_SUCCESS && size > 0 &&
            size <= pipeline_store::kMaxCacheBytes - 4096) {
            std::vector<std::uint8_t> data(size);
            if (vkGetPipelineCacheData(device_, pipelineCache_, &size, data.data()) == VK_SUCCESS) {
                data.resize(size);
                cacheOk = pipeline_store::write_file_atomic(storeDir_ / kCacheFile,
                                                            pipeline_store::pack_cache(storeId_, data));
            }
        }
        const pipeline_store::Manifest merged = pipeline_store::merge(earlier_, snapshot);
        const bool manifestOk =
            pipeline_store::write_file_atomic(storeDir_ / kManifestFile, pipeline_store::pack_manifest(merged));
        if (!cacheOk || !manifestOk)
            shader_log(1, "[GPU] pipeline cache: could not be written to " + storeDir_.string());
        if (!wait) saving_ = false;
    };
    if (wait)
        work();
    else
        warmer_.post(std::move(work));
}

void VulkanDevice::pipelineStoreFrame() {
    const Uint64 now = SDL_GetTicks();
    if (prewarmQueued_ > 0 && !prewarmLogged_) {
        const vkn::PipelineWarmer::Progress pr = warmer_.progress();
        if (pr.finished >= pr.queued) {
            prewarmLogged_ = true;
            char line[192];
            std::snprintf(line, sizeof line,
                          "[GPU] pipeline prewarm: %u pipelines ready %.0f ms after start (%.0f ms of builds on %u "
                          "threads, %u failed)",
                          pr.queued, static_cast<double>(now - prewarmStartMs_), pr.buildMs, warmer_.threads(),
                          pr.failed);
            shader_log(0, line);
            // The cache now holds what the workers built, taken or not.
            if (!cacheValid_ && !storeDir_.empty()) {
                storeDirty_ = true;
                lastNewPipelineMs_ = now;
            }
        }
    }
    if (buildLog_.built + buildLog_.taken > 0 && now - buildLogMs_ >= kBuildLogMs) {
        char line[256];
        std::snprintf(line, sizeof line,
                      "[GPU] pipelines: %u built on first use (%.1f ms, longest %.1f ms: %s), %u from the prewarm",
                      buildLog_.built, buildLog_.ms, buildLog_.longestMs,
                      buildLog_.longest.empty() ? "-" : buildLog_.longest.c_str(), buildLog_.taken);
        shader_log(0, line);
        buildLog_ = {};
        buildLogMs_ = now;
    }
    if (storeDirty_ && !saving_ && now - lastNewPipelineMs_ >= kStoreQuietMs) savePipelineStore(false);
}

void VulkanDevice::preparePipeline(const PipelineDesc& desc) {
    if (!desc_.pipelinePrewarm || !warmer_.running()) return;
    auto made = makePipe(desc);
    if (!made) return;
    const VkPipe& p = made.value();
    const VkFormat color = textureFormat(desc.colorFormat);
    const VkFormat depth = desc.hasDepthStencil ? textureFormat(desc.depthStencilFormat) : VK_FORMAT_UNDEFINED;
    vkn::PipelineWarmer::Job job;
    job.recipe = recipeFor(p, color, depth);
    job.key = pipeline_store::hash64(job.recipe.encode());
    {
        std::lock_guard<std::mutex> lock(storeMutex_);
        if (createdKeys_.contains(job.key)) return;
    }
    job.code = stageCode(p);
    job.target.layout = p.layout;
    job.target.renderPass = passApi_.compatiblePass(color, depth);
    warmer_.add(std::move(job));
}

PipelineWarmup VulkanDevice::pipelineWarmup() const {
    const vkn::PipelineWarmer::Progress pr = warmer_.progress();
    PipelineWarmup w;
    w.total = pr.queued;
    w.done = std::min(pr.finished, pr.queued);
    w.fresh = !cacheValid_ && prewarmQueued_ > 0;
    return w;
}

void VulkanDevice::destroy(TextureH handle) {
    if (handle == acquired_) return;
    if (auto t = textures_.remove(handle)) {
        if (t->swapchain) return;
        appVramBytes_ -= t->bytes;
        releaseTexture(*t);
    }
}

void VulkanDevice::destroy(BufferH handle) {
    if (auto b = buffers_.remove(handle)) {
        appVramBytes_ -= b->desc.size;
        VmaAllocator allocator = allocator_;
        const VkBuffer buffer = b->buffer;
        const VmaAllocation alloc = b->alloc;
        retire([allocator, buffer, alloc] { vmaDestroyBuffer(allocator, buffer, alloc); });
        for (const VkBuf::Spare& spare : b->spares) {
            appVramBytes_ -= b->desc.size;
            retire([allocator, spare] { vmaDestroyBuffer(allocator, spare.buffer, spare.alloc); });
        }
    }
}

void VulkanDevice::destroy(SamplerH handle) {
    if (auto s = samplers_.remove(handle)) {
        VkDevice dev = device_;
        const VkSampler sampler = s->sampler;
        retire([dev, sampler] { vkDestroySampler(dev, sampler, nullptr); });
    }
}

void VulkanDevice::destroy(ShaderH handle) { shaders_.remove(handle); }

void VulkanDevice::destroy(PipelineH handle) {
    if (auto p = pipelines_.remove(handle)) {
        if (boundPipeH_ == handle) {
            boundPipe_ = nullptr;
            boundPipeH_ = {};
            boundVk_ = VK_NULL_HANDLE;
        }
        VkDevice dev = device_;
        std::vector<VkPipeline> list;
        for (const VkPipe::Variant& v : p->variants) {
            const auto it = shared_.find(v.key);
            if (it == shared_.end() || --it->second.refs > 0) continue;
            list.push_back(it->second.pipeline);
            shared_.erase(it);
        }
        if (list.empty()) return;
        retire([dev, list] {
            for (VkPipeline pl : list) vkDestroyPipeline(dev, pl, nullptr);
        });
    }
}

// ---- host memory ------------------------------------------------------------------------------------------------

bool VulkanDevice::allocHost(Ring& ring, VkBufferUsageFlags usage, VkDeviceSize chunkSize, VkDeviceSize size,
                             VkDeviceSize align, VkDeviceSize reserve, Slice* out) {
    const VkDeviceSize need = std::max(size, reserve);
    auto fits = [&](std::uint32_t index, VkDeviceSize offset) {
        return index < ring.chunks.size() && offset + need <= ring.chunks[index].size;
    };
    VkDeviceSize offset = ring.used ? (ring.offset + align - 1) / align * align : 0;
    std::uint32_t index = ring.current;
    while (!fits(index, offset)) {
        if (index < ring.chunks.size() && (offset > 0 || ring.used)) {
            ++index;
            offset = 0;
            continue;
        }
        if (index < ring.chunks.size()) {  // an unused chunk too small: skip it
            ++index;
            continue;
        }
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = std::max(chunkSize, need);
        info.usage = usage;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        HostChunk c;
        VmaAllocationInfo got{};
        if (vmaCreateBuffer(allocator_, &info, &ai, &c.buffer, &c.alloc, &got) != VK_SUCCESS) return false;
        c.mapped = static_cast<std::byte*>(got.pMappedData);
        c.size = info.size;
        ring.chunks.push_back(c);
        index = static_cast<std::uint32_t>(ring.chunks.size() - 1);
        offset = 0;
    }
    ring.current = index;
    ring.offset = offset + size;
    ring.used = true;
    out->chunk = index;
    out->offset = offset;
    out->ptr = ring.chunks[index].mapped + offset;
    out->buffer = ring.chunks[index].buffer;
    return true;
}

void VulkanDevice::flushRing(Ring& ring) {
    if (!ring.used) return;
    for (std::uint32_t i = 0; i <= ring.current && i < ring.chunks.size(); ++i)
        vmaFlushAllocation(allocator_, ring.chunks[i].alloc, 0, VK_WHOLE_SIZE);
}

void VulkanDevice::freeRing(Ring& ring) {
    for (HostChunk& c : ring.chunks) vmaDestroyBuffer(allocator_, c.buffer, c.alloc);
    ring.chunks.clear();
}

VkDescriptorSet VulkanDevice::allocSet(VkDescriptorSetLayout layout) {
    Frame& f = frame();
    for (;;) {
        if (f.descPool >= f.descPools.size()) {
            const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096},
                                                  {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1024},
                                                  {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1024}};
            VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            info.maxSets = 2048;
            info.poolSizeCount = 3;
            info.pPoolSizes = sizes;
            VkDescriptorPool pool = VK_NULL_HANDLE;
            if (vkCreateDescriptorPool(device_, &info, nullptr, &pool) != VK_SUCCESS) return VK_NULL_HANDLE;
            f.descPools.push_back(pool);
        }
        VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        info.descriptorPool = f.descPools[f.descPool];
        info.descriptorSetCount = 1;
        info.pSetLayouts = &layout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        const VkResult r = vkAllocateDescriptorSets(device_, &info, &set);
        if (r == VK_SUCCESS) {
            ++counts_.descriptorSets;
            return set;
        }
        if (r != VK_ERROR_OUT_OF_POOL_MEMORY && r != VK_ERROR_FRAGMENTED_POOL) return VK_NULL_HANDLE;
        ++f.descPool;
    }
}

// ---- synchronisation --------------------------------------------------------------------------------------------

// Coarse but simple: every dependency is all commands to all commands. The game's frame is small; later stages that
// need overlap can narrow the stages per use.
void VulkanDevice::barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout from,
                           VkImageLayout to) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {aspect, 0, VK_REMAINING_MIP_LEVELS, 0, 1};
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    passApi_.barrier(cmd, dep);
    ++counts_.barriers;
}

void VulkanDevice::memoryBarrier() {
    VkMemoryBarrier2 b{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &b;
    passApi_.barrier(frame().cmd, dep);
    ++counts_.barriers;
}

void VulkanDevice::enterTransfer() {
    closePass();
    if (inTransfer_) return;
    inTransfer_ = true;
    ++counts_.transfers;
    beginSpan("transfer", 8);
    memoryBarrier();
}

// Textures touched by copies go back to their resting layout, and the copies are made visible to what follows.
void VulkanDevice::leaveTransfer() {
    if (!inTransfer_) return;
    inTransfer_ = false;
    for (TextureH h : touched_) {
        if (VkTexture* t = textures_.get(h); t && t->layout != t->rest) transition(*t, t->rest);
    }
    touched_.clear();
    memoryBarrier();
    endSpan();
}

void VulkanDevice::note(const char* fmt, ...) {
    char line[160];
    std::va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof line, fmt, args);
    va_end(args);
    trace_ += '\n';
    trace_ += line;
}

void VulkanDevice::touch(TextureH h) {
    if (std::find(touched_.begin(), touched_.end(), h) == touched_.end()) touched_.push_back(h);
}

void VulkanDevice::recordHoisted(VkCommandBuffer cmd) {
    if (hoisted_.empty()) return;
    // After whatever the submissions before still do with these buffers and images; before anything of this frame.
    VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    mb.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    mb.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    mb.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    mb.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &mb;
    passApi_.barrier(cmd, dep);
    for (const HoistedCopy& h : hoisted_) {
        if (h.image) {
            barrier(cmd, h.image, h.aspect, h.rest, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            vkCmdCopyBufferToImage(cmd, h.src, h.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &h.region);
            barrier(cmd, h.image, h.aspect, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, h.rest);
        } else {
            const VkBufferCopy region{h.srcOffset, h.dstOffset, h.size};
            vkCmdCopyBuffer(cmd, h.src, h.dst, 1, &region);
        }
    }
    passApi_.barrier(cmd, dep);
    hoisted_.clear();
}

// The buffer's draws of this frame keep its memory; its new contents go into memory no submission still reads.
bool VulkanDevice::cycleBuffer(VkBuf& b) {
    if (b.pinned) return false;
    const std::uint64_t serial = recordingSerial();
    auto it = std::find_if(b.spares.begin(), b.spares.end(),
                           [&](const VkBuf::Spare& s) { return s.busy <= completed_; });
    VkBuf::Spare next;
    if (it != b.spares.end()) {
        next = *it;
        b.spares.erase(it);
    } else {
        // Two frames in flight and a few rewrites a frame: past that many the frame waits for the transfer instead.
        constexpr std::size_t kMaxSpares = 8;
        if (b.spares.size() >= kMaxSpares) return false;
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = std::max<std::uint32_t>(b.desc.size, 4);
        info.usage =
            (b.desc.usage == BufferUsage::Index ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT : VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) |
            (b.desc.shaderRead ? VkBufferUsageFlags{VK_BUFFER_USAGE_STORAGE_BUFFER_BIT} : 0u) |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        if (vmaCreateBuffer(allocator_, &info, &ai, &next.buffer, &next.alloc, nullptr) != VK_SUCCESS) return false;
        appVramBytes_ += b.desc.size;
    }
    b.spares.push_back({b.buffer, b.alloc, serial});
    b.buffer = next.buffer;
    b.alloc = next.alloc;
    b.usedIn = 0;
    return true;
}

void VulkanDevice::recordPendingInits(VkCommandBuffer cmd) {
    for (const PendingInit& p : pendingInits_) barrier(cmd, p.image, p.aspect, VK_IMAGE_LAYOUT_UNDEFINED, p.layout);
    pendingInits_.clear();
}

// ---- timing -----------------------------------------------------------------------------------------------------

void VulkanDevice::beginSpan(const char* name, std::size_t length) {
    Frame& f = frame();
    if (!f.timed || !passTiming_ || openSpan_ >= 0 || f.queryCount + 2 >= kMaxQueries) return;
    Span s;
    const std::size_t n = std::min(length, s.name.size() - 1);
    std::memcpy(s.name.data(), name, n);
    s.begin = f.queryCount++;
    passApi_.timestamp(f.cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, f.queries, s.begin);
    f.spans.push_back(s);
    openSpan_ = static_cast<std::int32_t>(f.spans.size() - 1);
}

void VulkanDevice::endSpan() {
    Frame& f = frame();
    if (openSpan_ < 0) return;
    Span& s = f.spans[static_cast<std::size_t>(openSpan_)];
    s.end = f.queryCount++;
    passApi_.timestamp(f.cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, f.queries, s.end);
    openSpan_ = -1;
}

void VulkanDevice::collectTimings(Frame& f) {
    if (!f.timed || f.queryCount < 2) {
        f.timed = false;
        return;
    }
    f.timed = false;
    std::vector<std::uint64_t> stamps(f.queryCount);
    if (vkGetQueryPoolResults(device_, f.queries, 0, f.queryCount, stamps.size() * sizeof(std::uint64_t), stamps.data(),
                              sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
        return;
    auto ms = [&](std::uint32_t a, std::uint32_t b) {
        const std::uint64_t t0 = stamps[a] & timestampMask_, t1 = stamps[b] & timestampMask_;
        return t1 >= t0 ? static_cast<double>(t1 - t0) * timestampPeriodNs_ / 1.0e6 : 0.0;
    };
    gpuMs_ += ms(0, f.queryCount - 1);
    gpuTimed_ = true;
    for (const Span& s : f.spans) {
        if (s.end == 0) continue;
        std::uint32_t i = 0;
        while (i < passTimeCount_ && std::strcmp(passTimes_[i].name.data(), s.name.data()) != 0) ++i;
        if (i == passTimeCount_) {
            if (passTimeCount_ == passTimes_.size()) continue;
            passTimes_[i] = {};
            passTimes_[i].name = s.name;
            ++passTimeCount_;
        }
        passTimes_[i].ms += ms(s.begin, s.end);
    }
}

ProfileStats VulkanDevice::takeProfileStats() {
    ProfileStats out;
    out.drawCalls = drawCalls_;
    out.triangles = triangles_;
    drawCalls_ = 0;
    triangles_ = 0;
    if (gpuTimed_) out.gpuMs = gpuMs_;
    gpuMs_ = 0.0;
    gpuTimed_ = false;
    out.passes = passTimes_;
    out.passCount = passTimeCount_;
    passTimeCount_ = 0;
    out.counts = counts_;
    counts_ = {};
    out.patchTriangles = static_cast<std::int64_t>(patchTriangles_);
    out.pipelinesBuilt = pipelinesBuilt_;
    out.pipelineBuildMs = pipelineBuildMs_;
    out.textureUploadBytes = static_cast<std::int64_t>(textureUploadBytes_);
    patchTriangles_ = 0;
    pipelinesBuilt_ = 0;
    pipelineBuildMs_ = 0.0;
    textureUploadBytes_ = 0;
    out.appVramKb = static_cast<std::int64_t>(appVramBytes_ / 1024);
    if (memoryBudget_) {
        // The budget query may reach the kernel driver: a few times a second is enough for the overlay.
        const Uint64 now = SDL_GetTicks();
        if (budgetAtMs_ == 0 || now - budgetAtMs_ >= kBudgetPeriodMs) {
            budgetAtMs_ = now;
            vmaSetCurrentFrameIndex(allocator_, static_cast<std::uint32_t>(submitted_));
            const VkPhysicalDeviceMemoryProperties* mem = nullptr;
            vmaGetMemoryProperties(allocator_, &mem);
            std::vector<VmaBudget> budgets(mem->memoryHeapCount);
            vmaGetHeapBudgets(allocator_, budgets.data());
            std::uint64_t used = 0;
            for (std::uint32_t i = 0; i < mem->memoryHeapCount; ++i)
                if (mem->memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) used += budgets[i].usage;
            budgetKb_ = static_cast<std::int64_t>(used / 1024);
        }
        if (budgetKb_ >= 0) {
            out.appVramKb = budgetKb_;
            out.appVramFromDriver = true;
        }
    }
    return out;
}

// ---- frames -----------------------------------------------------------------------------------------------------

CommandList* VulkanDevice::begin() {
    if (recording_ || lost_) return nullptr;
    Frame& f = frame();
    if (VkResult r = vkWaitForFences(device_, 1, &f.fence, VK_TRUE, kFenceTimeoutNs); r != VK_SUCCESS) {
        if (r == VK_ERROR_DEVICE_LOST || r == VK_TIMEOUT) lost_ = true;
        return nullptr;
    }
    if (pacing_.framesInFlight <= 1) {
        // One frame in flight: the frame before (the other slot) must be done as well.
        Frame& other = frames_[(frameIndex_ + 1) % kFrames];
        if (VkResult r = vkWaitForFences(device_, 1, &other.fence, VK_TRUE, kFenceTimeoutNs); r != VK_SUCCESS) {
            if (r == VK_ERROR_DEVICE_LOST || r == VK_TIMEOUT) lost_ = true;
            return nullptr;
        }
        completed_ = std::max(completed_, other.serial);
    }
    if (pacing_.presentWait && presentWait_ && swapchain_ && presentId_ > shownFrom_ + 1) {
        // At most one frame waits for the display. A timeout only means the display is slower than that; the frame
        // goes on.
        constexpr std::uint64_t kPresentWaitNs = 100000000;
        const VkResult r = vkWaitForPresentKHR(device_, swapchain_, presentId_ - 1, kPresentWaitNs);
        if (r == VK_ERROR_DEVICE_LOST) {
            lost_ = true;
            return nullptr;
        }
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_ERROR_SURFACE_LOST_KHR) swapchainDirty_ = true;
    }
    completed_ = std::max(completed_, f.serial);
    passApi_.frameStart(frameIndex_);
    collectTimings(f);
    collectGarbage();
    vkResetFences(device_, 1, &f.fence);
    for (VkDescriptorPool p : f.descPools) vkResetDescriptorPool(device_, p, 0);
    f.descPool = 0;
    f.uniformSets.clear();
    resetRing(f.staging);
    resetRing(f.uniforms);
    f.spans.clear();
    f.queryCount = 0;
    vkResetCommandBuffer(f.cmd, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(f.cmd, &bi);
    f.timed = profiling_ && f.queries;
    if (f.timed) {
        vkCmdResetQueryPool(f.cmd, f.queries, 0, kMaxQueries);
        passApi_.timestamp(f.cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, f.queries, f.queryCount++);
    }
    recording_ = true;
    inPass_ = inTransfer_ = lingering_ = rendering_ = false;
    hoisted_.clear();
    tracing_ = traceFrames_ > 0;
    trace_.clear();
    if (tracing_) note("frame %llu", static_cast<unsigned long long>(submitted_ + 1));
    openSpan_ = -1;
    openSection_ = -1;
    storageDirty_ = true;
    boundPipe_ = nullptr;
    boundPipeH_ = {};
    boundVk_ = VK_NULL_HANDLE;
    samplerBind_ = {};
    samplersDirty_ = {true, true};
    uboOffset_ = {};
    for (auto& stage : uboData_)
        for (auto& d : stage) d.clear();
    uboChunk_ = 0;
    uniformsDirty_ = true;
    acquired_ = {};
    acquireWait_ = false;
    return &list_;
}

Result<void> VulkanDevice::submit() {
    if (!recording_) return Result<void>::fail("vulkan: submit without begin");
    pipelineStoreFrame();
    Frame& f = frame();
    closePass();
    leaveTransfer();
    VkTexture* swap = acquired_ && swapchain_ ? textures_.get(acquired_) : nullptr;
    if (swap && swap->swapchain) {
        barrier(f.cmd, swap->image, VK_IMAGE_ASPECT_COLOR_BIT, swap->layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        swap->layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    } else {
        swap = nullptr;
    }
    if (f.timed) passApi_.timestamp(f.cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, f.queries, f.queryCount++);
    vkEndCommandBuffer(f.cmd);
    recording_ = false;
    ++counts_.frames;
    if (tracing_) {
        tracing_ = false;
        --traceFrames_;
        shader_log(0, "[PERF] trace: " + trace_);
        trace_.clear();
    }
    flushRing(f.staging);
    flushRing(f.uniforms);

    VkCommandBufferSubmitInfo cmds[2]{};
    std::uint32_t cmdCount = 0;
    if (!pendingInits_.empty() || !hoisted_.empty()) {
        vkResetCommandBuffer(f.preamble, 0);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(f.preamble, &bi);
        recordPendingInits(f.preamble);
        recordHoisted(f.preamble);
        vkEndCommandBuffer(f.preamble);
        cmds[cmdCount].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmds[cmdCount++].commandBuffer = f.preamble;
    }
    cmds[cmdCount].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmds[cmdCount++].commandBuffer = f.cmd;
    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = f.acquired;
    wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    if (swap) signal.semaphore = renderDone_[swapIndex_];
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    si.waitSemaphoreInfoCount = acquireWait_ ? 1 : 0;
    si.pWaitSemaphoreInfos = &wait;
    si.commandBufferInfoCount = cmdCount;
    si.pCommandBufferInfos = cmds;
    si.signalSemaphoreInfoCount = swap ? 1 : 0;
    si.pSignalSemaphoreInfos = &signal;
    const VkResult sr = passApi_.submit(queue_, 1, &si, f.fence);
    acquired_ = {};
    acquireWait_ = false;
    frameIndex_ = (frameIndex_ + 1) % kFrames;
    if (sr != VK_SUCCESS) {
        // The fence will never signal: make it signalled so the slot's next wait does not stall.
        vkResetFences(device_, 1, &f.fence);
        VkSubmitInfo2 empty{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        if (sr != VK_ERROR_DEVICE_LOST) passApi_.submit(queue_, 1, &empty, f.fence);
        f.timed = false;
        return markLost("vkQueueSubmit2", sr);
    }
    f.serial = ++submitted_;
    if (!swap) return Result<void>::ok();

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &renderDone_[swapIndex_];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &swapIndex_;
    VkPresentIdKHR pid{VK_STRUCTURE_TYPE_PRESENT_ID_KHR};
    const std::uint64_t id = presentId_ + 1;
    if (presentWait_) {
        pid.swapchainCount = 1;
        pid.pPresentIds = &id;
        pi.pNext = &pid;
    }
    const VkResult pr = vkQueuePresentKHR(queue_, &pi);
    if (presentWait_ && (pr == VK_SUCCESS || pr == VK_SUBOPTIMAL_KHR)) presentId_ = id;
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || (pr == VK_SUBOPTIMAL_KHR && !swapRotated_) ||
        pr == VK_ERROR_SURFACE_LOST_KHR) {
        swapchainDirty_ = true;
        return Result<void>::ok();
    }
    if (pr != VK_SUCCESS && pr != VK_SUBOPTIMAL_KHR) return markLost("vkQueuePresentKHR", pr);
    return Result<void>::ok();
}

void VulkanDevice::waitIdle() {
    if (!device_) return;
    vkDeviceWaitIdle(device_);
    completed_ = submitted_;
    collectGarbage();
}

Result<void> VulkanDevice::readback(TextureH texture, const Rect& rect, std::span<std::byte> rgba8) {
    if (recording_) return Result<void>::fail("vulkan: readback with an open command list");
    if (lost_) return Result<void>::fail("vulkan: the device is lost");
    VkTexture* t = textures_.get(texture);
    if (!t) return Result<void>::fail("vulkan: readback of a released texture");
    const bool half = t->desc.format == TextureFormat::RGBA16F;
    if (t->swapchain || (t->desc.format != TextureFormat::RGBA8 && !half))
        return Result<void>::fail("vulkan: readback needs RGBA8 or RGBA16F");
    if (rect.w <= 0 || rect.h <= 0 || rect.x < 0 || rect.y < 0 ||
        static_cast<std::uint32_t>(rect.x + rect.w) > t->desc.width ||
        static_cast<std::uint32_t>(rect.y + rect.h) > t->desc.height)
        return Result<void>::fail("vulkan: readback outside the texture");
    const VkDeviceSize size = VkDeviceSize(rect.w) * VkDeviceSize(rect.h) * 4;
    if (rgba8.size() < size) return Result<void>::fail("vulkan: readback buffer small");
    const VkDeviceSize staged = half ? size * 2 : size;

    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = staged;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation alloc = nullptr;
    VmaAllocationInfo info{};
    if (VkResult r = vmaCreateBuffer(allocator_, &bi, &ai, &buffer, &alloc, &info); r != VK_SUCCESS)
        return Result<void>::fail(vkError("readback buffer", r));

    vkResetCommandBuffer(oneShot_, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(oneShot_, &begin);
    recordPendingInits(oneShot_);
    barrier(oneShot_, t->image, t->aspect, t->layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {rect.x, rect.y, 0};
    region.imageExtent = {static_cast<std::uint32_t>(rect.w), static_cast<std::uint32_t>(rect.h), 1};
    vkCmdCopyImageToBuffer(oneShot_, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region);
    barrier(oneShot_, t->image, t->aspect, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t->layout);
    vkEndCommandBuffer(oneShot_);
    VkCommandBufferSubmitInfo cmd{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmd.commandBuffer = oneShot_;
    VkSubmitInfo2 si{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cmd;
    vkResetFences(device_, 1, &oneShotFence_);
    VkResult r = passApi_.submit(queue_, 1, &si, oneShotFence_);
    if (r == VK_SUCCESS) r = vkWaitForFences(device_, 1, &oneShotFence_, VK_TRUE, kFenceTimeoutNs);
    if (r == VK_SUCCESS) {
        vmaInvalidateAllocation(allocator_, alloc, 0, VK_WHOLE_SIZE);
        if (half) {
            // Clamped to [0, 1] and rounded as the float-to-UNORM conversion of a blit does.
            const auto* src = static_cast<const std::uint16_t*>(info.pMappedData);
            auto* dst = reinterpret_cast<std::uint8_t*>(rgba8.data());
            for (VkDeviceSize i = 0; i < size; ++i)
                dst[i] = static_cast<std::uint8_t>(std::lround(std::clamp(halfToFloat(src[i]), 0.f, 1.f) * 255.f));
        } else {
            std::memcpy(rgba8.data(), info.pMappedData, size);
        }
    }
    vmaDestroyBuffer(allocator_, buffer, alloc);
    if (r != VK_SUCCESS) return markLost("readback", r);
    return Result<void>::ok();
}

// ---- command list -----------------------------------------------------------------------------------------------

void VulkanList::uploadTexture(TextureH texture, const Rect& rect, std::span<const std::byte> pixels) {
    uploadTextureLevel(texture, 0, rect, pixels);
}

void VulkanList::uploadTextureLevel(TextureH texture, std::uint32_t level, const Rect& rect,
                                    std::span<const std::byte> pixels) {
    VkTexture* t = dev_.textures_.get(texture);
    if (!t || t->swapchain || rect.w <= 0 || rect.h <= 0 || level >= t->desc.mipLevels) return;
    const VkDeviceSize size = VkDeviceSize(rect.w) * VkDeviceSize(rect.h) * bytesPerTexel(t->desc.format);
    if (pixels.size() < size) return;
    Slice s;
    if (!dev_.allocHost(dev_.frame().staging, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, kStagingChunk, size, 16, 0, &s)) return;
    std::memcpy(s.ptr, pixels.data(), size);
    dev_.textureUploadBytes_ += size;
    ++dev_.counts_.textureUploads;
    dev_.counts_.uploadBytes += size;
    VkBufferImageCopy region{};
    region.bufferOffset = s.offset;
    region.imageSubresource = {t->aspect & VK_IMAGE_ASPECT_COLOR_BIT ? VkImageAspectFlags(VK_IMAGE_ASPECT_COLOR_BIT)
                                                                     : VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT),
                               level, 0, 1};
    region.imageOffset = {rect.x, rect.y, 0};
    region.imageExtent = {static_cast<std::uint32_t>(rect.w), static_cast<std::uint32_t>(rect.h), 1};
    // A sampled texture the frame has not used yet takes its texels before the frame's commands.
    if (dev_.restructure_ && t->usedIn != dev_.recordingSerial() && t->layout == t->rest &&
        t->rest == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        HoistedCopy h;
        h.src = s.buffer;
        h.image = t->image;
        h.aspect = t->aspect;
        h.rest = t->rest;
        h.region = region;
        dev_.hoisted_.push_back(h);
        ++dev_.counts_.hoisted;
        if (dev_.tracing_) dev_.note("  upload texture %dx%d level %u (%llu bytes), ahead of the frame", rect.w, rect.h,
                                     level, static_cast<unsigned long long>(size));
        return;
    }
    dev_.enterTransfer();
    if (dev_.tracing_) dev_.note("  upload texture %dx%d level %u (%llu bytes)", rect.w, rect.h, level,
                                 static_cast<unsigned long long>(size));
    t->usedIn = dev_.recordingSerial();
    dev_.transition(*t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    dev_.touch(texture);
    vkCmdCopyBufferToImage(dev_.frame().cmd, s.buffer, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void VulkanList::uploadBuffer(BufferH buffer, std::uint32_t offset, std::span<const std::byte> data) {
    VkBuf* b = dev_.buffers_.get(buffer);
    if (!b || data.empty() || offset + data.size() > b->desc.size) return;
    Slice s;
    if (!dev_.allocHost(dev_.frame().staging, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, kStagingChunk, data.size(), 16, 0, &s))
        return;
    std::memcpy(s.ptr, data.data(), data.size());
    ++dev_.counts_.bufferUploads;
    dev_.counts_.uploadBytes += data.size();
    // A write from offset 0 starts the contents afresh: a buffer the frame already drew from takes new memory for
    // them (the draws before keep the old), so the copy can still go ahead of the frame.
    if (dev_.restructure_ && b->usedIn == dev_.recordingSerial() && offset == 0) dev_.cycleBuffer(*b);
    if (dev_.restructure_ && !b->pinned && b->usedIn != dev_.recordingSerial()) {
        HoistedCopy h;
        h.src = s.buffer;
        h.srcOffset = s.offset;
        h.dst = b->buffer;
        h.dstOffset = offset;
        h.size = data.size();
        dev_.hoisted_.push_back(h);
        ++dev_.counts_.hoisted;
        if (dev_.tracing_) dev_.note("  upload buffer %zu bytes at %u, ahead of the frame", data.size(), offset);
        return;
    }
    dev_.enterTransfer();
    if (dev_.tracing_) dev_.note("  upload buffer %zu bytes at %u", data.size(), offset);
    b->usedIn = dev_.recordingSerial();
    const VkBufferCopy region{s.offset, offset, data.size()};
    vkCmdCopyBuffer(dev_.frame().cmd, s.buffer, b->buffer, 1, &region);
}

void VulkanList::copyTexture(TextureH src, const Rect& srcRect, TextureH dst, std::int32_t dstX, std::int32_t dstY) {
    VkTexture* s = dev_.textures_.get(src);
    VkTexture* d = dev_.textures_.get(dst);
    if (!s || !d || srcRect.w <= 0 || srcRect.h <= 0) return;
    dev_.enterTransfer();
    ++dev_.counts_.copies;
    s->usedIn = d->usedIn = dev_.recordingSerial();
    if (dev_.tracing_) dev_.note("  copy %dx%d from %ux%u to %ux%u", srcRect.w, srcRect.h, s->desc.width, s->desc.height,
                                 d->desc.width, d->desc.height);
    VkImageLayout sl = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dl = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    if (s == d) sl = dl = VK_IMAGE_LAYOUT_GENERAL;
    dev_.transition(*s, sl);
    if (s != d) dev_.transition(*d, dl);
    dev_.touch(src);
    dev_.touch(dst);
    VkImageCopy region{};
    region.srcSubresource = {s->aspect, 0, 0, 1};
    region.srcOffset = {srcRect.x, srcRect.y, 0};
    region.dstSubresource = {d->aspect, 0, 0, 1};
    region.dstOffset = {dstX, dstY, 0};
    region.extent = {static_cast<std::uint32_t>(srcRect.w), static_cast<std::uint32_t>(srcRect.h), 1};
    vkCmdCopyImage(dev_.frame().cmd, s->image, sl, d->image, dl, 1, &region);
}

namespace {

// Clips one axis of a blit to both images, keeping the scale: a region outside an image is undefined behaviour, and
// a phone's GPU stops (device lost) on it, e.g. a present at the window's new size into the swapchain of the old one.
bool clipBlitAxis(int& s0, int& sw, int& d0, int& dw, int sSize, int dSize) {
    const double k = static_cast<double>(sw) / dw;
    double a0 = s0, a1 = s0 + sw, b0 = d0, b1 = d0 + dw;
    if (b0 < 0) a0 -= b0 * k, b0 = 0;
    if (b1 > dSize) a1 -= (b1 - dSize) * k, b1 = dSize;
    if (a0 < 0) b0 -= a0 / k, a0 = 0;
    if (a1 > sSize) b1 -= (a1 - sSize) / k, a1 = sSize;
    s0 = static_cast<int>(std::lround(a0));
    sw = static_cast<int>(std::lround(a1)) - s0;
    d0 = static_cast<int>(std::lround(b0));
    dw = static_cast<int>(std::lround(b1)) - d0;
    return sw > 0 && dw > 0;
}

}  // namespace

void VulkanList::blitTexture(TextureH src, const Rect& srcIn, TextureH dst, const Rect& dstIn, Filter filter) {
    VkTexture* s = dev_.textures_.get(src);
    VkTexture* d = dev_.textures_.get(dst);
    if (!s || !d || srcIn.w <= 0 || srcIn.h <= 0 || dstIn.w <= 0 || dstIn.h <= 0) return;
    Rect srcRect = srcIn, dstRect = dstIn;
    if (!clipBlitAxis(srcRect.x, srcRect.w, dstRect.x, dstRect.w, static_cast<int>(s->desc.width),
                      static_cast<int>(d->desc.width)) ||
        !clipBlitAxis(srcRect.y, srcRect.h, dstRect.y, dstRect.h, static_cast<int>(s->desc.height),
                      static_cast<int>(d->desc.height)))
        return;
    dev_.enterTransfer();
    ++dev_.counts_.blits;
    s->usedIn = d->usedIn = dev_.recordingSerial();
    if (dev_.tracing_)
        dev_.note("  blit %dx%d to %dx%d%s", srcRect.w, srcRect.h, dstRect.w, dstRect.h, d->swapchain ? " (swapchain)" : "");
    VkImageLayout sl = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dl = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    if (s == d) sl = dl = VK_IMAGE_LAYOUT_GENERAL;
    dev_.transition(*s, sl);
    if (s != d) dev_.transition(*d, dl);
    dev_.touch(src);
    dev_.touch(dst);
    if (srcRect.w == dstRect.w && srcRect.h == dstRect.h && s->format == d->format && dev_.restructure_) {
        // An unscaled blit is a copy, which is cheaper on most GPUs (a phone's blit is a draw of its own).
        VkImageCopy copy{};
        copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.srcOffset = {srcRect.x, srcRect.y, 0};
        copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.dstOffset = {dstRect.x, dstRect.y, 0};
        copy.extent = {static_cast<std::uint32_t>(srcRect.w), static_cast<std::uint32_t>(srcRect.h), 1};
        vkCmdCopyImage(dev_.frame().cmd, s->image, sl, d->image, dl, 1, &copy);
        return;
    }
    VkImageBlit region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.srcOffsets[0] = {srcRect.x, srcRect.y, 0};
    region.srcOffsets[1] = {srcRect.x + srcRect.w, srcRect.y + srcRect.h, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstOffsets[0] = {dstRect.x, dstRect.y, 0};
    region.dstOffsets[1] = {dstRect.x + dstRect.w, dstRect.y + dstRect.h, 1};
    vkCmdBlitImage(dev_.frame().cmd, s->image, sl, d->image, dl, 1, &region,
                   filter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
}

// Never reached: createTexture refuses multisampled textures on this backend (Caps::maxSamples is 1).
void VulkanList::resolveTexture(TextureH, TextureH) {}

namespace {

const char* loadName(LoadOp op) { return op == LoadOp::Load ? "load" : op == LoadOp::Clear ? "clear" : "-"; }

double attachmentMB(const VkTexture& t) {
    return static_cast<double>(t.desc.width) * t.desc.height * bytesPerTexel(t.desc.format) / (1024.0 * 1024.0);
}

}  // namespace

RenderPass* VulkanList::beginRenderPass(const RenderPassDesc& desc) {
    dev_.leaveTransfer();
    VkTexture* c = dev_.textures_.get(desc.color.texture);
    VkTexture* d = desc.depthStencil.texture ? dev_.textures_.get(desc.depthStencil.texture) : nullptr;
    const char* name = desc.name.empty() ? "other" : desc.name.data();
    const std::size_t nameLength = desc.name.empty() ? 5 : desc.name.size();
    VkCommandBuffer cmd = dev_.frame().cmd;
    // The pass before on the same attachments goes on: a clear becomes its load op (nothing drawn yet) or a clear
    // inside it, a load needs nothing. Once begun, it cannot when it does not store what this pass keeps.
    const bool same = dev_.lingering_ && c && desc.color.texture == dev_.passColor_ &&
                      desc.depthStencil.texture == dev_.passDepth_;
    if (same && (!dev_.rendering_ || ((desc.color.store == StoreOp::DontCare || dev_.passStoresColor_) &&
                                      (!d || desc.depthStencil.store == StoreOp::DontCare || dev_.passStoresDepth_)))) {
        dev_.lingering_ = false;
        dev_.endSpan();
        dev_.beginSpan(name, nameLength);
        if (!dev_.rendering_) {
            if (desc.color.load == LoadOp::Clear) {
                dev_.pendColor_.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                std::memcpy(dev_.pendColor_.clearValue.color.float32, desc.color.clear.data(), sizeof(float) * 4);
            }
            if (desc.color.store == StoreOp::Store) {
                dev_.pendColor_.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                dev_.passStoresColor_ = true;
            }
            if (d && desc.depthStencil.load == LoadOp::Clear) {
                dev_.pendDepth_.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                dev_.pendDepth_.clearValue.depthStencil = {desc.depthStencil.clearDepth, desc.depthStencil.clearStencil};
            }
            if (d && desc.depthStencil.store == StoreOp::Store) {
                dev_.pendDepth_.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                dev_.passStoresDepth_ = true;
            }
        } else {
            VkClearAttachment clears[2]{};
            std::uint32_t n = 0;
            if (desc.color.load == LoadOp::Clear) {
                clears[n].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                clears[n].colorAttachment = 0;
                std::memcpy(clears[n].clearValue.color.float32, desc.color.clear.data(), sizeof(float) * 4);
                ++n;
            }
            if (d && desc.depthStencil.load == LoadOp::Clear) {
                clears[n].aspectMask = d->aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
                clears[n].clearValue.depthStencil = {desc.depthStencil.clearDepth, desc.depthStencil.clearStencil};
                ++n;
            }
            if (n > 0) {
                const VkClearRect rect{{{0, 0}, {c->desc.width, c->desc.height}}, 0, 1};
                vkCmdClearAttachments(cmd, n, clears, 1, &rect);
                ++dev_.counts_.inPassClears;
            }
        }
        ++dev_.counts_.merged;
        if (dev_.tracing_)
            dev_.note("  merge %.*s: colour %s, depth %s%s", static_cast<int>(nameLength), name,
                      loadName(desc.color.load), d ? loadName(desc.depthStencil.load) : "none",
                      dev_.rendering_ ? "" : " (not begun yet)");
        dev_.pass_.setViewport({0.f, 0.f, float(c->desc.width), float(c->desc.height), 0.f, 1.f});
        dev_.pass_.setScissor({0, 0, int(c->desc.width), int(c->desc.height)});
        const float blend[4] = {0.f, 0.f, 0.f, 0.f};
        vkCmdSetBlendConstants(cmd, blend);
        vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
        return &dev_.pass_;
    }
    dev_.closePass();
    if (!c) return nullptr;
    if (desc.depthStencil.texture && !d) return nullptr;
    dev_.beginSpan(name, nameLength);
    dev_.transition(*c, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    if (d) dev_.transition(*d, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    const std::uint64_t serial = dev_.recordingSerial();
    c->usedIn = serial;
    if (d) d->usedIn = serial;

    VkRenderingAttachmentInfo& color = dev_.pendColor_;
    color = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = c->view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = vkLoad(desc.color.load);
    color.storeOp = vkStore(desc.color.store);
    std::memcpy(color.clearValue.color.float32, desc.color.clear.data(), sizeof(float) * 4);
    VkRenderingAttachmentInfo& depth = dev_.pendDepth_;
    depth = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (d) {
        depth.imageView = d->view;
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth.loadOp = vkLoad(desc.depthStencil.load);
        depth.storeOp = vkStore(desc.depthStencil.store);
        depth.clearValue.depthStencil = {desc.depthStencil.clearDepth, desc.depthStencil.clearStencil};
    }
    dev_.pendDepthOn_ = d != nullptr;
    dev_.pendStencil_ = d && hasStencil(d->format);
    dev_.pendExtent_ = {c->desc.width, c->desc.height};
    if (dev_.tracing_)
        dev_.note("pass %.*s %ux%u: colour %s/%s%s, depth %s/%s", static_cast<int>(nameLength), name, c->desc.width,
                  c->desc.height, loadName(desc.color.load), desc.color.store == StoreOp::Store ? "store" : "-",
                  c->swapchain ? " (swapchain)" : "", d ? loadName(desc.depthStencil.load) : "none",
                  d && desc.depthStencil.store == StoreOp::Store ? "store" : "-");
    dev_.passDraws_ = 0;
    dev_.inPass_ = true;
    dev_.rendering_ = false;
    dev_.lingering_ = false;
    dev_.passStoresColor_ = desc.color.store == StoreOp::Store;
    dev_.passStoresDepth_ = d && desc.depthStencil.store == StoreOp::Store;
    dev_.passColor_ = desc.color.texture;
    dev_.passDepth_ = d ? desc.depthStencil.texture : TextureH{};
    dev_.passColorFormat_ = c->format;
    dev_.passDepthFormat_ = d ? d->format : VK_FORMAT_UNDEFINED;
    dev_.boundVk_ = VK_NULL_HANDLE;  // the variant depends on the attachments
    if (!dev_.restructure_) dev_.beginRendering();
    dev_.pass_.setViewport({0.f, 0.f, float(c->desc.width), float(c->desc.height), 0.f, 1.f});
    dev_.pass_.setScissor({0, 0, int(c->desc.width), int(c->desc.height)});
    const float blend[4] = {0.f, 0.f, 0.f, 0.f};
    vkCmdSetBlendConstants(cmd, blend);
    vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
    return &dev_.pass_;
}

void VulkanDevice::beginRendering() {
    if (!inPass_ || rendering_) return;
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, pendExtent_};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &pendColor_;
    ri.pDepthAttachment = pendDepthOn_ ? &pendDepth_ : nullptr;
    ri.pStencilAttachment = pendStencil_ ? &pendDepth_ : nullptr;
    passApi_.begin(frame().cmd, ri, &passColorFormat_, passDepthFormat_);
    rendering_ = true;
    ++counts_.passes;
    if (pendColor_.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD) {
        ++counts_.loads;
        if (const VkTexture* c = textures_.get(passColor_)) counts_.attachmentMB += attachmentMB(*c);
    }
    if (pendDepthOn_ && pendDepth_.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD) {
        ++counts_.loads;
        if (const VkTexture* d = textures_.get(passDepth_)) counts_.attachmentMB += attachmentMB(*d);
    }
    if (tracing_)
        note("  begun: colour %s, depth %s", pendColor_.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD ? "load"
                                             : pendColor_.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? "clear" : "-",
             !pendDepthOn_ ? "none" : pendDepth_.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD ? "load"
                                   : pendDepth_.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? "clear" : "-");
}

void VulkanList::endRenderPass() {
    if (!dev_.inPass_ || dev_.lingering_) return;
    dev_.pass_.endTimedSection();
    if (dev_.restructure_)
        dev_.lingering_ = true;
    else
        dev_.closePass();
}

void VulkanDevice::closePass() {
    if (!inPass_) return;
    pass_.endTimedSection();
    // Nothing drawn: only a clear makes the pass worth beginning.
    if (!rendering_ && (pendColor_.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ||
                        (pendDepthOn_ && pendDepth_.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR)))
        beginRendering();
    VkTexture* c = textures_.get(passColor_);
    VkTexture* d = textures_.get(passDepth_);
    if (rendering_) {
        passApi_.end(frame().cmd);
        if (c && passStoresColor_) {
            ++counts_.stores;
            counts_.attachmentMB += attachmentMB(*c);
        }
        if (d && passStoresDepth_) {
            ++counts_.stores;
            counts_.attachmentMB += attachmentMB(*d);
        }
        if (tracing_) note("  end: %u draws", passDraws_);
    } else if (tracing_) {
        note("  dropped: nothing drawn or cleared");
    }
    inPass_ = false;
    rendering_ = false;
    lingering_ = false;
    if (c && c->rest != c->layout) transition(*c, c->rest);
    if (d && d->rest != d->layout) transition(*d, d->rest);
    passColor_ = {};
    passDepth_ = {};
    endSpan();
}

TextureH VulkanDevice::surfacelessTarget(std::uint32_t w, std::uint32_t h) {
    if (VkTexture* t = textures_.get(ownTarget_); t && t->desc.width == w && t->desc.height == h) return ownTarget_;
    if (ownTarget_) {
        const TextureH old = ownTarget_;
        ownTarget_ = {};
        destroy(old);
    }
    TextureDesc d;
    d.width = w;
    d.height = h;
    d.usage = kUsageColorTarget;
    d.name = "surfaceless frame";
    if (auto t = createTexture(d)) ownTarget_ = t.value();
    return ownTarget_;
}

TextureH VulkanList::acquireSwapchainTexture(std::uint32_t* width, std::uint32_t* height) {
    auto give = [&](TextureH h) {
        VkTexture* t = dev_.textures_.get(h);
        if (width) *width = t ? t->desc.width : 0;
        if (height) *height = t ? t->desc.height : 0;
        return t ? h : TextureH{};
    };
    if (dev_.acquired_) return give(dev_.acquired_);
    SDL_Window* window = dev_.desc_.window;
    if (!window) return give({});
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) return give({});
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    if (pw <= 0 || ph <= 0) return give({});
    if (dev_.surfaceStale_.exchange(false) && dev_.surface_) {
        dev_.closePass();
        dev_.renewSurface();
    }
    if (!dev_.surface_) {
        dev_.acquired_ = dev_.surfacelessTarget(static_cast<std::uint32_t>(pw), static_cast<std::uint32_t>(ph));
        return give(dev_.acquired_);
    }
    if (dev_.swapchainDirty_ || !dev_.swapchain_ || dev_.swapExtent_.width != static_cast<std::uint32_t>(pw) ||
        dev_.swapExtent_.height != static_cast<std::uint32_t>(ph)) {
        dev_.closePass();
        if (!dev_.createSwapchain() || !dev_.swapchain_) return give({});
    }
    Frame& f = dev_.frame();
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::uint32_t index = 0;
        const VkResult r =
            vkAcquireNextImageKHR(dev_.device_, dev_.swapchain_, kAcquireTimeoutNs, f.acquired, VK_NULL_HANDLE, &index);
        if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) {
            if (r == VK_SUBOPTIMAL_KHR && !dev_.swapRotated_) dev_.swapchainDirty_ = true;
            dev_.swapIndex_ = index;
            dev_.acquireWait_ = true;
            dev_.acquired_ = dev_.swapTextures_[index];
            if (VkTexture* t = dev_.textures_.get(dev_.acquired_)) t->layout = VK_IMAGE_LAYOUT_UNDEFINED;
            return give(dev_.acquired_);
        }
        if (r != VK_ERROR_OUT_OF_DATE_KHR) {
            if (r == VK_ERROR_DEVICE_LOST) dev_.lost_ = true;
            return give({});
        }
        dev_.closePass();
        if (!dev_.createSwapchain() || !dev_.swapchain_) return give({});
    }
    return give({});
}

// ---- render pass ------------------------------------------------------------------------------------------------

void VulkanPass::bindPipeline(PipelineH pipeline) {
    VkPipe* p = dev_.pipelines_.get(pipeline);
    if (!p || pipeline == dev_.boundPipeH_) return;
    dev_.boundPipe_ = p;
    dev_.boundPipeH_ = pipeline;
}

// The SDL3 GPU flip: a negative height makes NDC +Y point up as in GL (VK_KHR_maintenance1, core since 1.1).
void VulkanPass::setViewport(const Viewport& v) {
    const VkViewport vp{v.x, v.y + v.h, v.w, -v.h, v.minDepth, v.maxDepth};
    vkCmdSetViewport(dev_.frame().cmd, 0, 1, &vp);
}

void VulkanPass::setScissor(const Rect& r) {
    std::int32_t x = r.x, y = r.y, w = r.w, h = r.h;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    const VkRect2D sc{{x, y}, {static_cast<std::uint32_t>(std::max(w, 0)), static_cast<std::uint32_t>(std::max(h, 0))}};
    vkCmdSetScissor(dev_.frame().cmd, 0, 1, &sc);
}

void VulkanPass::setBlendConstants(const std::array<float, 4>& c) {
    vkCmdSetBlendConstants(dev_.frame().cmd, c.data());
}

void VulkanPass::setStencilReference(std::uint8_t reference) {
    vkCmdSetStencilReference(dev_.frame().cmd, VK_STENCIL_FACE_FRONT_AND_BACK, reference);
}

void VulkanPass::bindVertexBuffer(BufferH buffer, std::uint32_t offset) {
    VkBuf* b = dev_.buffers_.get(buffer);
    if (!b) return;
    b->usedIn = dev_.recordingSerial();
    const VkDeviceSize off = offset;
    vkCmdBindVertexBuffers(dev_.frame().cmd, 0, 1, &b->buffer, &off);
}

void VulkanPass::bindIndexBuffer(BufferH buffer, IndexType type, std::uint32_t offset) {
    VkBuf* b = dev_.buffers_.get(buffer);
    if (!b) return;
    b->usedIn = dev_.recordingSerial();
    vkCmdBindIndexBuffer(dev_.frame().cmd, b->buffer, offset,
                         type == IndexType::U32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
}

void VulkanPass::bindSampler(ShaderStage stage, std::uint32_t slot, TextureH texture, SamplerH sampler) {
    if (slot >= kMaxSamplerSlots) return;
    // As on SDL3 GPU, a texture without a sampler binds nothing and the slot keeps what it had.
    if (texture && !sampler) return;
    const std::size_t s = stage == ShaderStage::Vertex ? 0 : 1;
    // Callers rebind per draw; an unchanged slot must not cost a new descriptor set (Mali reloads its tables).
    VulkanDevice::SamplerBinding& b = dev_.samplerBind_[s][slot];
    if (b.texture == texture && b.sampler == sampler) return;
    b = {texture, sampler};
    dev_.samplersDirty_[s] = true;
}

void VulkanPass::pushUniforms(ShaderStage stage, std::uint32_t slot, std::span<const std::byte> data) {
    if (slot >= kMaxUniformSlots || data.empty()) return;
    const std::size_t s = stage == ShaderStage::Vertex ? 0 : 1;
    const VkDeviceSize size = std::min<VkDeviceSize>(data.size(), dev_.uboRange_);
    Frame& f = dev_.frame();
    Slice slice;
    if (!dev_.allocHost(f.uniforms, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, kUniformChunk, size, dev_.uboAlign_,
                        dev_.uboRange_, &slice))
        return;
    std::memcpy(slice.ptr, data.data(), size);
    dev_.uboData_[s][slot].assign(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(size));
    dev_.uboOffset_[s][slot] = static_cast<std::uint32_t>(slice.offset);
    if (slice.chunk != dev_.uboChunk_) {
        dev_.uboChunk_ = slice.chunk;
        dev_.pushLiveUniforms();
    }
    dev_.uniformsDirty_ = true;
}

// Every slot's offset must point into the chunk the uniform sets bind: the live data moves into the new chunk.
void VulkanDevice::pushLiveUniforms() {
    Frame& f = frame();
    for (std::size_t s = 0; s < 2; ++s) {
        for (std::size_t i = 0; i < kMaxUniformSlots; ++i) {
            const std::vector<std::byte>& d = uboData_[s][i];
            if (d.empty()) continue;
            Slice slice;
            if (!allocHost(f.uniforms, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, kUniformChunk, d.size(), uboAlign_,
                           uboRange_, &slice))
                return;
            if (slice.chunk != uboChunk_) return;  // cannot happen: a chunk holds far more than the live slots
            std::memcpy(slice.ptr, d.data(), d.size());
            uboOffset_[s][i] = static_cast<std::uint32_t>(slice.offset);
        }
    }
}

bool VulkanDevice::flushDraw() {
    if (!boundPipe_ || !inPass_ || lingering_) return false;
    beginRendering();
    Frame& f = frame();
    const std::uint64_t serial = recordingSerial();
    VkPipe& p = *boundPipe_;
    const VkPipeline pipeline = variant(p, passColorFormat_, passDepthFormat_);
    if (!pipeline) return false;
    if (pipeline != boundVk_) {
        vkCmdBindPipeline(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        ++counts_.pipelineBinds;
        boundVk_ = pipeline;
        samplersDirty_ = {true, true};
        uniformsDirty_ = true;
        storageDirty_ = true;
    }
    if (p.counts[kStorageBuffers] > 0 && storageDirty_) {
        const std::uint32_t count = p.counts[kStorageBuffers];
        std::array<VkDescriptorBufferInfo, kMaxStorageSlots> infos{};
        std::array<VkWriteDescriptorSet, kMaxStorageSlots> writes{};
        for (std::uint32_t i = 0; i < count; ++i) {
            VkBuf* b = buffers_.get(storageBind_[i].buffer);
            if (!b || !b->desc.shaderRead) return false;  // nothing valid to read: the draw is skipped
            b->usedIn = serial;
            infos[i] = {b->buffer, storageBind_[i].offset, VK_WHOLE_SIZE};
        }
        VkDescriptorSet set = allocSet(p.setLayouts[kStorageBuffers]);
        if (!set) return false;
        for (std::uint32_t i = 0; i < count; ++i) {
            writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[i].dstSet = set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &infos[i];
        }
        vkUpdateDescriptorSets(device_, count, writes.data(), 0, nullptr);
        vkCmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, kStorageBuffers, 1, &set, 0, nullptr);
        storageDirty_ = false;
    }
    for (std::size_t s = 0; s < 2; ++s) {
        const std::uint32_t kind = s == 0 ? kVertexSamplers : kFragmentSamplers;
        const std::uint32_t count = p.counts[kind];
        if (count == 0 || !samplersDirty_[s]) continue;
        VkDescriptorSet set = allocSet(p.setLayouts[kind]);
        if (!set) return false;
        std::array<VkDescriptorImageInfo, kMaxSamplerSlots> images{};
        std::array<VkWriteDescriptorSet, kMaxSamplerSlots> writes{};
        for (std::uint32_t i = 0; i < count; ++i) {
            const SamplerBinding& b = samplerBind_[s][i];
            VkTexture* t = textures_.get(b.texture);
            VkSamp* sm = samplers_.get(b.sampler);
            if (!t || !(t->desc.usage & kUsageSampled)) t = textures_.get(dummy_);
            t->usedIn = serial;
            images[i] = {sm ? sm->sampler : dummySampler_, t->sampledView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[i].dstSet = set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &images[i];
        }
        vkUpdateDescriptorSets(device_, count, writes.data(), 0, nullptr);
        vkCmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, kind, 1, &set, 0, nullptr);
        samplersDirty_[s] = false;
    }
    if (uniformsDirty_) {
        for (std::size_t s = 0; s < 2; ++s) {
            const std::uint32_t kind = s == 0 ? kVertexUniforms : kFragmentUniforms;
            const std::uint32_t count = p.counts[kind];
            if (count == 0) continue;
            if (uboChunk_ >= f.uniforms.chunks.size()) {
                // Nothing pushed yet this frame: give the slots a chunk to point at.
                Slice slice;
                if (!allocHost(f.uniforms, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, kUniformChunk, uboAlign_, uboAlign_,
                               uboRange_, &slice))
                    return false;
                uboChunk_ = slice.chunk;
            }
            const std::uint64_t key = (std::uint64_t{kind} << 56) | (std::uint64_t{count} << 48) | uboChunk_;
            VkDescriptorSet set = VK_NULL_HANDLE;
            if (auto it = f.uniformSets.find(key); it != f.uniformSets.end()) {
                set = it->second;
            } else {
                set = allocSet(p.setLayouts[kind]);
                if (!set) return false;
                std::array<VkDescriptorBufferInfo, kMaxUniformSlots> infos{};
                std::array<VkWriteDescriptorSet, kMaxUniformSlots> writes{};
                for (std::uint32_t i = 0; i < count; ++i) {
                    infos[i] = {f.uniforms.chunks[uboChunk_].buffer, 0, uboRange_};
                    writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                    writes[i].dstSet = set;
                    writes[i].dstBinding = i;
                    writes[i].descriptorCount = 1;
                    writes[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                    writes[i].pBufferInfo = &infos[i];
                }
                vkUpdateDescriptorSets(device_, count, writes.data(), 0, nullptr);
                f.uniformSets[key] = set;
            }
            std::array<std::uint32_t, kMaxUniformSlots> offsets{};
            for (std::uint32_t i = 0; i < count; ++i) offsets[i] = uboOffset_[s][i];
            vkCmdBindDescriptorSets(f.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, kind, 1, &set, count,
                                    offsets.data());
        }
        uniformsDirty_ = false;
    }
    return true;
}

void VulkanPass::draw(std::uint32_t vertexCount, std::uint32_t firstVertex) {
    if (!vertexCount || !dev_.flushDraw()) return;
    ++dev_.drawCalls_;
    ++dev_.passDraws_;
    dev_.triangles_ += vertexCount / 3;
    if (dev_.boundPipe_->tcs) dev_.patchTriangles_ += vertexCount / 3;
    vkCmdDraw(dev_.frame().cmd, vertexCount, 1, firstVertex, 0);
}

void VulkanPass::drawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex, std::int32_t vertexOffset) {
    if (!indexCount || !dev_.flushDraw()) return;
    ++dev_.drawCalls_;
    ++dev_.passDraws_;
    dev_.triangles_ += indexCount / 3;
    if (dev_.boundPipe_->tcs) dev_.patchTriangles_ += indexCount / 3;
    vkCmdDrawIndexed(dev_.frame().cmd, indexCount, 1, firstIndex, vertexOffset, 0);
}

void VulkanPass::bindStorageBuffer(std::uint32_t slot, BufferH buffer, std::uint32_t offset) {
    if (slot >= kMaxStorageSlots) return;
    VulkanDevice::StorageBinding& b = dev_.storageBind_[slot];
    if (b.buffer == buffer && b.offset == offset) return;
    b = {buffer, offset};
    dev_.storageDirty_ = true;
}

void VulkanPass::drawMeshTasks(std::uint32_t taskGroups) {
    if (!taskGroups || !dev_.meshShaders_ || !dev_.boundPipe_ || !dev_.boundPipe_->mesh || !dev_.flushDraw()) return;
    ++dev_.drawCalls_;
    ++dev_.passDraws_;
    vkCmdDrawMeshTasksEXT(dev_.frame().cmd, taskGroups, 1, 1);
}

void VulkanPass::beginTimedSection(std::string_view name) {
    Frame& f = dev_.frame();
    if (!f.timed || !dev_.passTiming_ || !dev_.inPass_ || dev_.openSection_ >= 0 || f.queryCount + 4 >= kMaxQueries)
        return;
    Span s;
    const std::size_t n = std::min(name.size(), s.name.size() - 1);
    std::memcpy(s.name.data(), name.data(), n);
    s.begin = f.queryCount++;
    dev_.passApi_.timestamp(f.cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, f.queries, s.begin);
    f.spans.push_back(s);
    dev_.openSection_ = static_cast<std::int32_t>(f.spans.size() - 1);
}

void VulkanPass::endTimedSection() {
    Frame& f = dev_.frame();
    if (dev_.openSection_ < 0) return;
    Span& s = f.spans[static_cast<std::size_t>(dev_.openSection_)];
    s.end = f.queryCount++;
    dev_.passApi_.timestamp(f.cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, f.queries, s.end);
    dev_.openSection_ = -1;
}

}  // namespace

bool loadVulkanLibrary() {
#ifdef __APPLE__
    if (SDL_Vulkan_GetVkGetInstanceProcAddr() != nullptr) return true;
    // The package's own MoltenVK first, then a loader (it finds MoltenVK through its ICD file, and the validation
    // layer), then MoltenVK alone.
    std::vector<std::string> candidates;
    // Beside the executable: in an application bundle that is Contents/MacOS, which SDL_GetBasePath does not answer.
    char exe[4096];
    std::uint32_t size = sizeof(exe);
    if (_NSGetExecutablePath(exe, &size) == 0) {
        std::string dir(exe);
        dir.erase(dir.find_last_of('/') + 1);
        candidates.push_back(dir + "libvulkan.1.dylib");
        candidates.push_back(dir + "libMoltenVK.dylib");
    }
    for (const char* name :
         {"libvulkan.1.dylib", "/usr/local/lib/libvulkan.1.dylib", "/opt/homebrew/lib/libvulkan.1.dylib",
          "libMoltenVK.dylib", "/usr/local/lib/libMoltenVK.dylib", "/opt/homebrew/lib/libMoltenVK.dylib"}) {
        candidates.emplace_back(name);
    }
    for (const std::string& path : candidates) {
        if (SDL_Vulkan_LoadLibrary(path.c_str())) {
            shader_log(0, "vulkan: library " + path);
            return true;
        }
    }
    return false;
#else
    return true;
#endif
}

Result<std::unique_ptr<Device>> createVulkanDevice(const VulkanDeviceDesc& desc) {
    auto device = std::make_unique<VulkanDevice>(desc);
    if (auto r = device->init(); !r) return Result<std::unique_ptr<Device>>::fail(r.error());
    return Result<std::unique_ptr<Device>>::ok(std::move(device));
}

const VulkanCaps* vulkanCaps(const Device& device) {
    if (const auto* d = dynamic_cast<const VulkanDevice*>(&device)) return &d->vulkanCaps();
    return nullptr;
}

void vulkanSetPresentMode(Device& device, bool vsync) {
    if (auto* d = dynamic_cast<VulkanDevice*>(&device)) d->setPresentMode(vsync);
}

void vulkanSetFramePacing(Device& device, const VulkanFramePacing& pacing) {
    if (auto* d = dynamic_cast<VulkanDevice*>(&device)) d->setFramePacing(pacing);
}

bool vulkanPresentWaitSupported(const Device& device) {
    const auto* d = dynamic_cast<const VulkanDevice*>(&device);
    return d && d->presentWaitSupported();
}

const VulkanNative* vulkanNative(Device& device) {
    static thread_local VulkanNative s_native;
    auto* d = dynamic_cast<VulkanDevice*>(&device);
    if (!d) return nullptr;
    s_native = d->native();
    return &s_native;
}

VkCommandBuffer vulkanNativeCommands(Device& device) {
    auto* d = dynamic_cast<VulkanDevice*>(&device);
    return d ? d->nativeCommands() : VK_NULL_HANDLE;
}

std::uint32_t vulkanFrameSlot(Device& device) {
    auto* d = dynamic_cast<VulkanDevice*>(&device);
    return d ? d->frameSlot() : 0;
}

void vulkanRetire(Device& device, std::function<void()> release) {
    if (auto* d = dynamic_cast<VulkanDevice*>(&device)) d->retireNative(std::move(release));
}

bool vulkanImageInfo(Device& device, TextureH texture, VulkanImageInfo* out) {
    auto* d = dynamic_cast<VulkanDevice*>(&device);
    return d && d->imageInfo(texture, out);
}

VkBuffer vulkanBuffer(Device& device, BufferH buffer) {
    auto* d = dynamic_cast<VulkanDevice*>(&device);
    return d ? d->nativeBuffer(buffer) : VK_NULL_HANDLE;
}

TextureH vulkanAdoptImage(Device& device, VkImage image, VkImageView view, VkFormat format, std::uint32_t width,
                          std::uint32_t height) {
    auto* d = dynamic_cast<VulkanDevice*>(&device);
    return d ? d->adoptImage(image, view, format, width, height) : TextureH{};
}

void vulkanBeginSpan(Device& device, const char* name) {
    if (auto* d = dynamic_cast<VulkanDevice*>(&device)) d->beginNativeSpan(name);
}

void vulkanEndSpan(Device& device) {
    if (auto* d = dynamic_cast<VulkanDevice*>(&device)) d->endNativeSpan();
}

void vulkanSetHdrOutput(Device& device, const VulkanHdrOutput& hdr) {
    if (auto* d = dynamic_cast<VulkanDevice*>(&device)) d->setHdrOutput(hdr);
}

VulkanHdrStatus vulkanHdrStatus(const Device& device) {
    if (const auto* d = dynamic_cast<const VulkanDevice*>(&device)) return d->hdrStatus();
    VulkanHdrStatus s;
    s.block = VulkanHdrBlock::OtherBackend;
    s.reason = hdrBlockText(s.block);
    return s;
}

void VulkanDevice::applyHdrMetadata() {
    // BT.2020 primaries and the D65 white point (CIE 1931 xy), the mastering range the tonemapper targets.
    VkHdrMetadataEXT m{VK_STRUCTURE_TYPE_HDR_METADATA_EXT};
    m.displayPrimaryRed = {0.708f, 0.292f};
    m.displayPrimaryGreen = {0.170f, 0.797f};
    m.displayPrimaryBlue = {0.131f, 0.046f};
    m.whitePoint = {0.3127f, 0.3290f};
    m.maxLuminance = hdr_.peakNits;
    m.minLuminance = 0.f;
    m.maxContentLightLevel = hdr_.peakNits;
    m.maxFrameAverageLightLevel = hdr_.paperWhiteNits;
    vkSetHdrMetadataEXT(device_, 1, &swapchain_, &m);
}

std::string describeVulkanCaps(const VulkanCaps& c) {
    auto yes = [](bool b) { return b ? "yes" : "no"; };
    char line[512];
    std::string out;
    std::snprintf(line, sizeof line,
                  "Vulkan %s (device %s), %s GPU %s, vendor 0x%04x device 0x%04x, driver %s %s (0x%08x)",
                  versionText(c.apiVersion).c_str(), versionText(c.deviceApiVersion).c_str(), c.deviceType.c_str(),
                  c.deviceName.c_str(), c.vendorId, c.deviceId, c.driverName.empty() ? "?" : c.driverName.c_str(),
                  c.driverInfo.c_str(), c.driverVersion);
    out += line;
    std::snprintf(line, sizeof line,
                  "\nray tracing: acceleration_structure %s, ray_query %s, ray_tracing_pipeline %s; mesh_shader %s; "
                  "tessellation %s; subgroup %u (fragment quad operations %s)",
                  yes(c.accelerationStructure), yes(c.rayQuery), yes(c.rayTracingPipeline), yes(c.meshShader),
                  yes(c.tessellationShader), c.subgroupSize, yes(c.fragmentQuadOps));
    out += line;
    std::snprintf(line, sizeof line,
                  "\npasses: %s; barriers and submits: %s",
                  c.dynamicRendering ? "dynamic rendering" : "render pass objects (no dynamic rendering)",
                  c.synchronization2 ? "synchronization2" : "Vulkan 1.0 (no synchronization2)");
    out += line;
    std::snprintf(line, sizeof line,
                  "\npresent: %s, swapchain_colorspace %s, hdr_metadata %s; memory_budget %s; VRAM %llu MB, budget "
                  "%llu MB; timestamps %s; validation %s",
                  c.surfaceless ? "surfaceless (own images)" : "swapchain", yes(c.swapchainColorspace),
                  yes(c.hdrMetadata), yes(c.memoryBudget), static_cast<unsigned long long>(c.deviceLocalBytes >> 20),
                  static_cast<unsigned long long>(c.vramBudgetBytes >> 20), yes(c.timestamps), yes(c.validation));
    out += line;
    return out;
}

// ---- native access (rhi_vulkan_native.h) -------------------------------------------------------------------------

namespace {

struct NativeAccess {
    static VulkanDevice* of(Device& device) { return dynamic_cast<VulkanDevice*>(&device); }

    static void fill(VulkanDevice& d, vkn::Context& c) {
        c.physical = d.physical_;
        c.device = d.device_;
        c.allocator = d.allocator_;
        c.pipelineCache = d.pipelineCache_;
        c.queueFamily = d.queueFamily_;
        std::uint32_t n = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d.physical_, &n, nullptr);
        std::vector<VkQueueFamilyProperties> families(n);
        vkGetPhysicalDeviceQueueFamilyProperties(d.physical_, &n, families.data());
        c.queueFlags = d.queueFamily_ < n ? families[d.queueFamily_].queueFlags : 0;
        c.rendering = &d.passApi_;
    }

    static VkCommandBuffer record(VulkanDevice& d) {
        if (!d.recording_ || d.lost_) return VK_NULL_HANDLE;
        d.closePass();
        d.leaveTransfer();
        // The caller binds pipelines and sets of its own.
        d.boundVk_ = VK_NULL_HANDLE;
        d.boundPipe_ = nullptr;
        d.boundPipeH_ = {};
        d.samplersDirty_ = {true, true};
        d.uniformsDirty_ = true;
        return d.frame().cmd;
    }

    static std::uint32_t slot(VulkanDevice& d) { return d.frameIndex_; }
    static std::uint64_t serial(VulkanDevice& d) { return d.submitted_ + 1; }

    static bool image(VulkanDevice& d, TextureH texture, vkn::Image* out) {
        VkTexture* t = d.textures_.get(texture);
        if (!t) return false;
        t->usedIn = d.recordingSerial();
        out->image = t->image;
        out->view = t->view;
        out->sampledView = t->sampledView;
        out->format = t->format;
        out->aspect = t->aspect;
        out->width = t->desc.width;
        out->height = t->desc.height;
        out->layout = t->layout;
        out->rest = t->rest;
        return true;
    }

    static void transition(VulkanDevice& d, TextureH texture, VkImageLayout layout) {
        VkTexture* t = d.textures_.get(texture);
        if (!t || !d.recording_) return;
        t->usedIn = d.recordingSerial();
        if (t->layout == layout) return;
        d.closePass();
        d.transition(*t, layout);
    }

    static void beginSpan(VulkanDevice& d, const char* name) { d.beginSpan(name, std::strlen(name)); }
    static void endSpan(VulkanDevice& d) { d.endSpan(); }
    static void retire(VulkanDevice& d, std::function<void()> release) { d.retire(std::move(release)); }
};

}  // namespace

namespace vkn {

const Context* context(Device& device) {
    VulkanDevice* d = NativeAccess::of(device);
    if (!d) return nullptr;
    static Context c;
    NativeAccess::fill(*d, c);
    return &c;
}

VkCommandBuffer record(Device& device) {
    VulkanDevice* d = NativeAccess::of(device);
    return d ? NativeAccess::record(*d) : VK_NULL_HANDLE;
}

std::uint32_t frameSlots() { return kFrames; }

std::uint32_t frameSlot(Device& device) {
    VulkanDevice* d = NativeAccess::of(device);
    return d ? NativeAccess::slot(*d) : 0;
}

std::uint64_t frameSerial(Device& device) {
    VulkanDevice* d = NativeAccess::of(device);
    return d ? NativeAccess::serial(*d) : 0;
}

bool image(Device& device, TextureH texture, Image* out) {
    VulkanDevice* d = NativeAccess::of(device);
    return d && NativeAccess::image(*d, texture, out);
}

void transition(Device& device, TextureH texture, VkImageLayout layout) {
    if (VulkanDevice* d = NativeAccess::of(device)) NativeAccess::transition(*d, texture, layout);
}

void beginSpan(Device& device, const char* name) {
    if (VulkanDevice* d = NativeAccess::of(device)) NativeAccess::beginSpan(*d, name);
}

void endSpan(Device& device) {
    if (VulkanDevice* d = NativeAccess::of(device)) NativeAccess::endSpan(*d);
}

void retire(Device& device, std::function<void()> release) {
    if (VulkanDevice* d = NativeAccess::of(device)) NativeAccess::retire(*d, std::move(release));
}

}  // namespace vkn

}  // namespace sh::rhi
