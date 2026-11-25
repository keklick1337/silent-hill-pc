// SPDX-License-Identifier: GPL-3.0-or-later
//
// TAA, TAAU and the spatial upscale of the native Vulkan backend (vk_temporal.h). Own images live in the general
// layout for their whole life, except the motion pass's depth, which stays a depth attachment.
// The `tiler` layout (phones) draws the resolve and the spatial upscale as fragment passes into the history, which
// rests in the shader-read layout between them: a tiler keeps colour attachments compressed (AFBC on Mali) but not
// storage images, and fragment work after fragment work needs no drain of the compute queue. Its motion buffer is
// RGBA16F, cleared by its own pass and skipped when nothing moved; the history depth is the history's alpha; the
// sharpening is done by the composite.

#include "render/rhi/vk_temporal.h"

#include "render/rhi/rhi_vulkan_native.h"
#include "render/rhi/vk_rendering.h"
#include "render/rhi/shaders/embedded.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace sh::rhi::embedded {
Shader temporalMotionVert();
Shader temporalMotionFrag();
Shader temporalResolveComp();
Shader temporalSpatialComp();
Shader temporalSharpenComp();
Shader temporalCompositeVert();
Shader temporalCompositeFrag();
Shader temporalResolveFrag();
Shader temporalSpatialFrag();
Shader temporalCompositeSharpenFrag();
}  // namespace sh::rhi::embedded

namespace sh::rhi {

namespace {

constexpr VkFormat kMotionFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
constexpr VkFormat kMotionDepthFormat = VK_FORMAT_D32_SFLOAT;
constexpr VkFormat kHistoryFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat kHistoryDepthFormat = VK_FORMAT_R32_SFLOAT;
// Motion is only drawn for moving meshes, which are near: 16-bit floats keep their picture offset to a small fraction
// of a pixel and their depth well inside the disocclusion test's 10 %.
constexpr VkFormat kTilerMotionFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

enum Binding : std::uint32_t {
    kUniforms = 0,
    kColor = 1,
    kDepth = 2,
    kMotion = 3,
    kHistoryIn = 4,
    kHistoryDepthIn = 5,
    kHistoryOut = 6,
    kHistoryDepthOut = 7,
    kResult = 8,
    kStage = 9,
    kResultSampled = 10,
    kBindingCount = 11
};

// std140 Temporal block of shaders/temporal/common.glsl.
struct Block {
    float src[4];
    float dst[4];
    float proj[4];
    float prevProj[4];
    float jitter[4];
    float reproj[12];
    float params[4];
};
static_assert(sizeof(Block) == 4 * 4 * 9);

struct GpuImage {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation alloc = nullptr;
    std::uint32_t w = 0, h = 0;
};

struct Slot {
    VkBuffer ubo = VK_NULL_HANDLE;
    VmaAllocation uboAlloc = nullptr;
    void* mapped = nullptr;
    VkBuffer vbo = VK_NULL_HANDLE;
    VmaAllocation vboAlloc = nullptr;
    void* vboMapped = nullptr;
    std::size_t vboBytes = 0;
    VkDescriptorPool pool = VK_NULL_HANDLE;
};

std::string vkFail(const char* what, VkResult r) {
    return std::string("temporal: ") + what + " (VkResult " + std::to_string(r) + ")";
}

bool formatHas(VkPhysicalDevice physical, VkFormat format, VkFormatFeatureFlags need) {
    VkFormatProperties p{};
    vkGetPhysicalDeviceFormatProperties(physical, format, &p);
    return (p.optimalTilingFeatures & need) == need;
}

class VulkanTemporal final : public Temporal {
public:
    VulkanTemporal(Device& device, const vkn::Context& ctx, bool tiler)
        : device_(device), ctx_(ctx), tiler_(tiler), motionFormat_(tiler ? kTilerMotionFormat : kMotionFormat) {}
    ~VulkanTemporal() override;

    Result<void> init();
    Result<void> apply(const TemporalInputs& in) override;
    void reset() override { historyValid_ = false; }
    TextureH result() const override { return resultPending_ ? historyTex_[resultIndex_] : TextureH{}; }
    Result<void> applyResult() override;

private:
    VkShaderModule module(const embedded::Shader& s);
    Result<void> makeImage(GpuImage& img, VkFormat format, std::uint32_t w, std::uint32_t h, VkImageUsageFlags usage);
    void releaseImage(GpuImage& img);
    Result<void> ensureImages(VkCommandBuffer cmd, std::uint32_t sw, std::uint32_t sh, std::uint32_t dw,
                              std::uint32_t dh);
    Result<void> ensureVertices(Slot& slot, std::size_t bytes);
    // `target`: a full-screen fragment shader writing all four channels of `color` (not cached).
    VkPipeline graphicsPipeline(VkFormat color, bool motion, VkShaderModule target = VK_NULL_HANDLE);
    void barrier(VkCommandBuffer cmd);
    void recordTiler(VkCommandBuffer cmd, VkDescriptorSet set, const Slot& slot, const TemporalInputs& in,
                     const vkn::Image& color, const float src[4], const float dst[4], VkPipeline composite);
    void recordComposite(VkCommandBuffer cmd, VkDescriptorSet set, TextureH colorTex, const vkn::Image& color,
                         const float dst[4], VkPipeline composite);

    Device& device_;
    vkn::Context ctx_;
    bool tiler_ = false;
    VkFormat motionFormat_ = kMotionFormat;
    VkPipeline resolveFrag_ = VK_NULL_HANDLE, spatialFrag_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline resolve_ = VK_NULL_HANDLE, spatial_ = VK_NULL_HANDLE, sharpen_ = VK_NULL_HANDLE;
    VkPipeline motion_ = VK_NULL_HANDLE;
    VkShaderModule motionVs_ = VK_NULL_HANDLE, motionFs_ = VK_NULL_HANDLE;
    VkShaderModule compositeVs_ = VK_NULL_HANDLE, compositeFs_ = VK_NULL_HANDLE;
    struct Variant {
        VkFormat color;
        VkPipeline pipeline;
    };
    std::vector<Variant> composites_;
    VkSampler nearest_ = VK_NULL_HANDLE, linear_ = VK_NULL_HANDLE;
    std::vector<Slot> slots_;
    GpuImage motionImage_, motionDepth_, result_;
    GpuImage history_[2], historyDepth_[2];
    int current_ = 0;  // the history written by the next apply
    // A tiler's histories as RHI textures (Temporal::result) and the composite a folded apply left owed.
    TextureH historyTex_[2];
    bool historyAdopted_[2] = {false, false};  // historyTex_ stands for the history image made last
    bool resultPending_ = false;
    int resultIndex_ = 0;
    VkDescriptorSet resultSet_ = VK_NULL_HANDLE;
    TextureH resultColor_;
    float resultDst_[4] = {};
    bool historyValid_ = false;
    std::uint64_t appliedSerial_ = 0;
};

VulkanTemporal::~VulkanTemporal() {
    VkDevice dev = ctx_.device;
    VmaAllocator vma = ctx_.allocator;
    std::vector<VkPipeline> pipes{resolve_, spatial_, sharpen_, motion_, resolveFrag_, spatialFrag_};
    for (const Variant& v : composites_) pipes.push_back(v.pipeline);
    std::vector<GpuImage> images{motionImage_, motionDepth_,     result_,         history_[0],
                                 history_[1],  historyDepth_[0], historyDepth_[1]};
    for (TextureH t : historyTex_)
        if (t) device_.destroy(t);
    std::vector<Slot> slots = slots_;
    std::vector<VkShaderModule> modules{motionVs_, motionFs_, compositeVs_, compositeFs_};
    VkSampler nearest = nearest_, linear = linear_;
    VkPipelineLayout layout = layout_;
    VkDescriptorSetLayout setLayout = setLayout_;
    vkn::retire(device_, [=]() {
        for (VkPipeline p : pipes)
            if (p) vkDestroyPipeline(dev, p, nullptr);
        for (const GpuImage& i : images) {
            if (i.view) vkDestroyImageView(dev, i.view, nullptr);
            if (i.image) vmaDestroyImage(vma, i.image, i.alloc);
        }
        for (const Slot& s : slots) {
            if (s.pool) vkDestroyDescriptorPool(dev, s.pool, nullptr);
            if (s.ubo) vmaDestroyBuffer(vma, s.ubo, s.uboAlloc);
            if (s.vbo) vmaDestroyBuffer(vma, s.vbo, s.vboAlloc);
        }
        for (VkShaderModule m : modules)
            if (m) vkDestroyShaderModule(dev, m, nullptr);
        if (nearest) vkDestroySampler(dev, nearest, nullptr);
        if (linear) vkDestroySampler(dev, linear, nullptr);
        if (layout) vkDestroyPipelineLayout(dev, layout, nullptr);
        if (setLayout) vkDestroyDescriptorSetLayout(dev, setLayout, nullptr);
    });
}

VkShaderModule VulkanTemporal::module(const embedded::Shader& s) {
    if (s.spirv.empty()) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = s.spirv.size_bytes();
    ci.pCode = s.spirv.data();
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(ctx_.device, &ci, nullptr, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
    return m;
}

const VkDescriptorType kTypes[kBindingCount] = {
    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};

Result<void> VulkanTemporal::init() {
    VkDevice dev = ctx_.device;
    std::array<VkDescriptorSetLayoutBinding, kBindingCount> b{};
    for (std::uint32_t i = 0; i < kBindingCount; ++i) {
        b[i].binding = i;
        b[i].descriptorType = kTypes[i];
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;
    }
    VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sl.bindingCount = kBindingCount;
    sl.pBindings = b.data();
    if (VkResult r = vkCreateDescriptorSetLayout(dev, &sl, nullptr, &setLayout_); r != VK_SUCCESS)
        return Result<void>::fail(vkFail("set layout", r));
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &setLayout_;
    if (VkResult r = vkCreatePipelineLayout(dev, &pl, nullptr, &layout_); r != VK_SUCCESS)
        return Result<void>::fail(vkFail("pipeline layout", r));

    auto compute = [&](const embedded::Shader& s, VkPipeline* out) -> bool {
        VkShaderModule m = module(s);
        if (!m) return false;
        VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        ci.stage.module = m;
        ci.stage.pName = "main";
        ci.layout = layout_;
        const VkResult r = vkCreateComputePipelines(dev, ctx_.pipelineCache, 1, &ci, nullptr, out);
        vkDestroyShaderModule(dev, m, nullptr);
        return r == VK_SUCCESS;
    };
    if (!tiler_ &&
        (!compute(embedded::temporalResolveComp(), &resolve_) || !compute(embedded::temporalSpatialComp(), &spatial_) ||
         !compute(embedded::temporalSharpenComp(), &sharpen_)))
        return Result<void>::fail("temporal: the compute shaders are missing or were refused");
    motionVs_ = module(embedded::temporalMotionVert());
    motionFs_ = module(embedded::temporalMotionFrag());
    compositeVs_ = module(embedded::temporalCompositeVert());
    compositeFs_ = module(tiler_ ? embedded::temporalCompositeSharpenFrag() : embedded::temporalCompositeFrag());
    if (!motionVs_ || !motionFs_ || !compositeVs_ || !compositeFs_)
        return Result<void>::fail("temporal: the graphics shaders are missing");
    motion_ = graphicsPipeline(motionFormat_, true);
    if (!motion_) return Result<void>::fail("temporal: no motion pipeline");
    if (tiler_) {
        VkShaderModule resolveFs = module(embedded::temporalResolveFrag());
        VkShaderModule spatialFs = module(embedded::temporalSpatialFrag());
        if (resolveFs) resolveFrag_ = graphicsPipeline(kHistoryFormat, false, resolveFs);
        if (spatialFs) spatialFrag_ = graphicsPipeline(kHistoryFormat, false, spatialFs);
        if (resolveFs) vkDestroyShaderModule(dev, resolveFs, nullptr);
        if (spatialFs) vkDestroyShaderModule(dev, spatialFs, nullptr);
        if (!resolveFrag_ || !spatialFrag_) return Result<void>::fail("temporal: no fragment resolve pipeline");
    }

    VkSamplerCreateInfo s{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.maxLod = 0.0f;
    s.magFilter = s.minFilter = VK_FILTER_NEAREST;
    if (VkResult r = vkCreateSampler(dev, &s, nullptr, &nearest_); r != VK_SUCCESS)
        return Result<void>::fail(vkFail("sampler", r));
    s.magFilter = s.minFilter = VK_FILTER_LINEAR;
    if (VkResult r = vkCreateSampler(dev, &s, nullptr, &linear_); r != VK_SUCCESS)
        return Result<void>::fail(vkFail("sampler", r));

    slots_.resize(vkn::frameSlots());
    for (Slot& slot : slots_) {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = sizeof(Block);
        bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo info{};
        if (VkResult r = vmaCreateBuffer(ctx_.allocator, &bi, &ai, &slot.ubo, &slot.uboAlloc, &info); r != VK_SUCCESS)
            return Result<void>::fail(vkFail("uniform buffer", r));
        slot.mapped = info.pMappedData;
        const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
                                              {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 7},
                                              {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3}};
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.maxSets = 1;
        pi.poolSizeCount = 3;
        pi.pPoolSizes = sizes;
        if (VkResult r = vkCreateDescriptorPool(dev, &pi, nullptr, &slot.pool); r != VK_SUCCESS)
            return Result<void>::fail(vkFail("descriptor pool", r));
    }
    return Result<void>::ok();
}

Result<void> VulkanTemporal::makeImage(GpuImage& img, VkFormat format, std::uint32_t w, std::uint32_t h,
                                       VkImageUsageFlags usage) {
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.extent = {w, h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (VkResult r = vmaCreateImage(ctx_.allocator, &ci, &ai, &img.image, &img.alloc, nullptr); r != VK_SUCCESS)
        return Result<void>::fail(vkFail("image", r));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = img.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    const bool depth = format == kMotionDepthFormat;
    vi.subresourceRange = {
        static_cast<VkImageAspectFlags>(depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 1, 0, 1};
    if (VkResult r = vkCreateImageView(ctx_.device, &vi, nullptr, &img.view); r != VK_SUCCESS)
        return Result<void>::fail(vkFail("image view", r));
    img.w = w;
    img.h = h;
    return Result<void>::ok();
}

void VulkanTemporal::releaseImage(GpuImage& img) {
    if (!img.image) return;
    VkDevice dev = ctx_.device;
    VmaAllocator vma = ctx_.allocator;
    const GpuImage old = img;
    vkn::retire(device_, [=]() {
        vkDestroyImageView(dev, old.view, nullptr);
        vmaDestroyImage(vma, old.image, old.alloc);
    });
    img = {};
}

Result<void> VulkanTemporal::ensureImages(VkCommandBuffer cmd, std::uint32_t sw, std::uint32_t sh, std::uint32_t dw,
                                          std::uint32_t dh) {
    std::vector<VkImageMemoryBarrier2> barriers;
    const VkImageLayout rest = tiler_ ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
    auto want = [&](GpuImage& img, VkFormat format, std::uint32_t w, std::uint32_t h,
                    VkImageUsageFlags usage) -> Result<void> {
        if (img.image && img.w == w && img.h == h) return Result<void>::ok();
        releaseImage(img);
        if (auto r = makeImage(img, format, w, h, usage); !r) return r;
        const bool depth = format == kMotionDepthFormat;
        VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : rest;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = img.image;
        b.subresourceRange = {
            static_cast<VkImageAspectFlags>(depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, 1, 0, 1};
        barriers.push_back(b);
        historyValid_ = false;
        return Result<void>::ok();
    };
    // A tiler's images have no storage or transfer use, so its driver may keep them compressed.
    const VkImageUsageFlags drawn = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    const VkImageUsageFlags store = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (auto r = want(motionImage_, motionFormat_, sw, sh, tiler_ ? drawn : drawn | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        !r)
        return r;
    if (auto r = want(motionDepth_, kMotionDepthFormat, sw, sh, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT); !r)
        return r;
    if (!tiler_)
        if (auto r = want(result_, kHistoryFormat, dw, dh, store); !r) return r;
    for (int i = 0; i < 2; ++i) {
        if (!history_[i].image || history_[i].w != dw || history_[i].h != dh) historyAdopted_[i] = false;
        if (auto r = want(history_[i], kHistoryFormat, dw, dh, tiler_ ? drawn : store); !r) return r;
        if (!tiler_)
            if (auto r = want(historyDepth_[i], kHistoryDepthFormat, dw, dh, store); !r) return r;
    }
    if (!barriers.empty()) {
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = static_cast<std::uint32_t>(barriers.size());
        dep.pImageMemoryBarriers = barriers.data();
        ctx_.rendering->barrier(cmd, dep);
    }
    // A tiler's histories rest in the shader-read layout, as an adopted RHI texture must.
    for (int i = 0; tiler_ && i < 2; ++i) {
        if (historyAdopted_[i]) continue;
        if (historyTex_[i]) device_.destroy(historyTex_[i]);
        historyTex_[i] = vulkanAdoptImage(device_, history_[i].image, history_[i].view, kHistoryFormat, dw, dh);
        historyAdopted_[i] = true;
        resultPending_ = false;
    }
    return Result<void>::ok();
}

Result<void> VulkanTemporal::ensureVertices(Slot& slot, std::size_t bytes) {
    if (slot.vbo && slot.vboBytes >= bytes) return Result<void>::ok();
    if (slot.vbo) {
        VmaAllocator vma = ctx_.allocator;
        VkBuffer old = slot.vbo;
        VmaAllocation oldAlloc = slot.vboAlloc;
        vkn::retire(device_, [=]() { vmaDestroyBuffer(vma, old, oldAlloc); });
        slot.vbo = VK_NULL_HANDLE;
    }
    std::size_t size = 64 * 1024;
    while (size < bytes) size *= 2;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo info{};
    if (VkResult r = vmaCreateBuffer(ctx_.allocator, &bi, &ai, &slot.vbo, &slot.vboAlloc, &info); r != VK_SUCCESS)
        return Result<void>::fail(vkFail("vertex buffer", r));
    slot.vboMapped = info.pMappedData;
    slot.vboBytes = size;
    return Result<void>::ok();
}

// The motion triangles (depth-tested among themselves, into the motion buffer) or the composite (a full-screen
// triangle writing colour only, so the scene target keeps its alpha).
VkPipeline VulkanTemporal::graphicsPipeline(VkFormat color, bool motion, VkShaderModule target) {
    if (!motion && !target)
        for (const Variant& v : composites_)
            if (v.color == color) return v.pipeline;
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = motion ? motionVs_ : compositeVs_;
    stages[0].pName = "main";
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = motion ? motionFs_ : target ? target : compositeFs_;
    stages[1].pName = "main";
    const VkVertexInputBindingDescription binding{0, sizeof(TemporalVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attrs[3] = {{0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0},
                                                        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16},
                                                        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 32}};
    VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (motion) {
        vin.vertexBindingDescriptionCount = 1;
        vin.pVertexBindingDescriptions = &binding;
        vin.vertexAttributeDescriptionCount = 3;
        vin.pVertexAttributeDescriptions = attrs;
    }
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    if (motion) {
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    }
    VkPipelineColorBlendAttachmentState att{};
    att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    if (motion || target) att.colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &att;
    const VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ri.colorAttachmentCount = 1;
    ri.pColorAttachmentFormats = &color;
    if (motion) ri.depthAttachmentFormat = kMotionDepthFormat;
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    ctx_.rendering->pipeline(ci, ri);
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vin;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &dy;
    ci.layout = layout_;
    VkPipeline p = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(ctx_.device, ctx_.pipelineCache, 1, &ci, nullptr, &p) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    if (!motion && !target) composites_.push_back({color, p});
    return p;
}

// Coarse on purpose: the passes are few and each reads what the one before wrote.
void VulkanTemporal::barrier(VkCommandBuffer cmd) {
    VkMemoryBarrier2 b{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &b;
    ctx_.rendering->barrier(cmd, dep);
}

void imageBarrier(std::vector<VkImageMemoryBarrier2>& out, VkImage image, VkImageLayout from, VkImageLayout to,
                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                  VkAccessFlags2 dstAccess) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = srcStage;
    b.srcAccessMask = srcAccess;
    b.dstStageMask = dstStage;
    b.dstAccessMask = dstAccess;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    out.push_back(b);
}

void rect(const std::array<float, 4>& r, float out[4]) {
    for (int i = 0; i < 4; ++i) out[i] = std::floor(r[static_cast<std::size_t>(i)]);
}

// Every pass is a fragment pass. Between them only the image the next one reads changes hands (attachment output to
// fragment reads), so the tiler may already bin the next pass while the last one shades.
void VulkanTemporal::recordTiler(VkCommandBuffer cmd, VkDescriptorSet set, const Slot& slot, const TemporalInputs& in,
                                 const vkn::Image& color, const float src[4], const float dst[4],
                                 VkPipeline composite) {
    const VkImageLayout ro = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, att = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    const VkPipelineStageFlags2 output = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkPipelineStageFlags2 fragment = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    const VkAccessFlags2 written = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    const VkAccessFlags2 read = VK_ACCESS_2_SHADER_READ_BIT;
    auto dependency = [&](const VkMemoryBarrier2* memory, const std::vector<VkImageMemoryBarrier2>& images) {
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.memoryBarrierCount = memory ? 1 : 0;
        dep.pMemoryBarriers = memory;
        dep.imageMemoryBarrierCount = static_cast<std::uint32_t>(images.size());
        dep.pImageMemoryBarriers = images.data();
        ctx_.rendering->barrier(cmd, dep);
    };
    auto fullScreen = [&](const GpuImage& target, VkAttachmentLoadOp load, VkPipeline pipeline) {
        VkRenderingAttachmentInfo a{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        a.imageView = target.view;
        a.imageLayout = att;
        a.loadOp = load;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        const VkRect2D area{{0, 0}, {target.w, target.h}};
        ri.renderArea = area;
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &a;
        ctx_.rendering->begin(cmd, ri, &kHistoryFormat, VK_FORMAT_UNDEFINED);
        const VkViewport viewport{0.0f, 0.0f, static_cast<float>(target.w), static_cast<float>(target.h), 0.0f, 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &area);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set, 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        ctx_.rendering->end(cmd);
    };

    const bool temporal = in.mode != TemporalMode::Spatial;
    const bool moving = temporal && !in.dynamic.empty();
    const GpuImage& histOut = history_[current_];
    // The scene's writes before everything; the old contents of what is drawn now are not needed.
    VkMemoryBarrier2 scene{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    scene.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    scene.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    scene.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    scene.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    std::vector<VkImageMemoryBarrier2> images;
    imageBarrier(images, histOut.image, VK_IMAGE_LAYOUT_UNDEFINED, att, fragment, 0, output, written);
    if (moving) imageBarrier(images, motionImage_.image, VK_IMAGE_LAYOUT_UNDEFINED, att, fragment, 0, output, written);
    dependency(&scene, images);

    if (moving) {
        VkRenderingAttachmentInfo a{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        a.imageView = motionImage_.view;
        a.imageLayout = att;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingAttachmentInfo datt{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        datt.imageView = motionDepth_.view;
        datt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        datt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        datt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        datt.clearValue.depthStencil = {1.0f, 0};
        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        const VkRect2D area{{0, 0}, {motionImage_.w, motionImage_.h}};
        ri.renderArea = area;
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &a;
        ri.pDepthAttachment = &datt;
        ctx_.rendering->begin(cmd, ri, &motionFormat_, kMotionDepthFormat);
        // Flipped like the backend's own passes, so NDC +Y is up as the scene drew it.
        const VkViewport viewport{0.0f, src[3], src[2], -src[3], 0.0f, 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &area);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, motion_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set, 0, nullptr);
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &slot.vbo, &offset);
        vkCmdDraw(cmd, static_cast<std::uint32_t>(in.dynamic.size()), 1, 0, 0);
        ctx_.rendering->end(cmd);
        images.clear();
        imageBarrier(images, motionImage_.image, att, ro, output, written, fragment, read);
        dependency(nullptr, images);
    }

    fullScreen(histOut, VK_ATTACHMENT_LOAD_OP_DONT_CARE, temporal ? resolveFrag_ : spatialFrag_);
    images.clear();
    imageBarrier(images, histOut.image, att, ro, output, written, fragment, read);
    dependency(nullptr, images);
    vkn::endSpan(device_);
    if (in.fold) {
        resultPending_ = true;
        resultIndex_ = current_;
        resultSet_ = set;
        resultColor_ = in.color;
        std::memcpy(resultDst_, dst, sizeof resultDst_);
        return;
    }
    recordComposite(cmd, set, in.color, color, dst, composite);
}

// The tiler's result over `dst` of the scene target, sharpened on the way (composite_sharpen.frag).
void VulkanTemporal::recordComposite(VkCommandBuffer cmd, VkDescriptorSet set, TextureH colorTex,
                                     const vkn::Image& color, const float dst[4], VkPipeline composite) {
    const VkImageLayout att = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    vkn::beginSpan(device_, "temporal composite");
    vkn::transition(device_, colorTex, att);
    const auto dw = static_cast<std::uint32_t>(dst[2]), dh = static_cast<std::uint32_t>(dst[3]);
    VkRenderingAttachmentInfo a{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    a.imageView = color.view;
    a.imageLayout = att;
    a.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    const VkRect2D area{{static_cast<std::int32_t>(dst[0]), static_cast<std::int32_t>(dst[1])}, {dw, dh}};
    ri.renderArea = area;
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &a;
    ctx_.rendering->begin(cmd, ri, &color.format, VK_FORMAT_UNDEFINED);
    const VkViewport viewport{dst[0], dst[1], dst[2], dst[3], 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &area);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, composite);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    ctx_.rendering->end(cmd);
    vkn::transition(device_, colorTex, color.rest);
    vkn::endSpan(device_);
}

Result<void> VulkanTemporal::applyResult() {
    if (!resultPending_) return Result<void>::fail("temporal: no result");
    resultPending_ = false;
    vkn::Image color;
    if (!vkn::image(device_, resultColor_, &color)) return Result<void>::fail("temporal: no scene target");
    const VkPipeline composite = graphicsPipeline(color.format, false);
    if (!composite) return Result<void>::fail("temporal: no composite pipeline for the scene format");
    VkCommandBuffer cmd = vkn::record(device_);
    if (!cmd) return Result<void>::fail("temporal: no frame is being recorded");
    recordComposite(cmd, resultSet_, resultColor_, color, resultDst_, composite);
    return Result<void>::ok();
}

Result<void> VulkanTemporal::apply(const TemporalInputs& in) {
    resultPending_ = false;
    const std::uint64_t serial = vkn::frameSerial(device_);
    if (serial == appliedSerial_) return Result<void>::ok();
    vkn::Image color, depth;
    if (!vkn::image(device_, in.color, &color) || !vkn::image(device_, in.depth, &depth))
        return Result<void>::fail("temporal: no scene target");
    if (!(depth.aspect & VK_IMAGE_ASPECT_DEPTH_BIT) || depth.rest != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        return Result<void>::fail("temporal: the scene depth is not sampled");
    float src[4], dst[4];
    rect(in.src, src);
    rect(in.dst, dst);
    const float cw = static_cast<float>(color.width), ch = static_cast<float>(color.height);
    for (float* r : {src, dst}) {
        r[0] = std::clamp(r[0], 0.0f, cw);
        r[1] = std::clamp(r[1], 0.0f, ch);
        r[2] = std::min(r[2], cw - r[0]);
        r[3] = std::min(r[3], ch - r[1]);
    }
    if (src[2] < 8.0f || src[3] < 8.0f || dst[2] < 8.0f || dst[3] < 8.0f) return Result<void>::ok();

    VkCommandBuffer cmd = vkn::record(device_);
    if (!cmd) return Result<void>::fail("temporal: no frame is being recorded");
    appliedSerial_ = serial;
    const auto sw = static_cast<std::uint32_t>(src[2]), sh = static_cast<std::uint32_t>(src[3]);
    const auto dw = static_cast<std::uint32_t>(dst[2]), dh = static_cast<std::uint32_t>(dst[3]);
    if (auto r = ensureImages(cmd, sw, sh, dw, dh); !r) return r;
    const VkPipeline composite = graphicsPipeline(color.format, false);
    if (!composite) return Result<void>::fail("temporal: no composite pipeline for the scene format");
    Slot& slot = slots_[vkn::frameSlot(device_) % slots_.size()];
    const std::size_t vbytes = in.dynamic.size_bytes();
    if (vbytes > 0) {
        if (auto r = ensureVertices(slot, vbytes); !r) return r;
        std::memcpy(slot.vboMapped, in.dynamic.data(), vbytes);
        vmaFlushAllocation(ctx_.allocator, slot.vboAlloc, 0, VK_WHOLE_SIZE);
    }

    const bool temporal = in.mode != TemporalMode::Spatial;
    const bool reset = in.reset || !historyValid_;
    Block blk{};
    std::memcpy(blk.src, src, sizeof src);
    std::memcpy(blk.dst, dst, sizeof dst);
    std::memcpy(blk.proj, in.proj.data(), sizeof blk.proj);
    std::memcpy(blk.prevProj, in.prev_proj.data(), sizeof blk.prevProj);
    const float jitter[4] = {in.jitter[0], in.jitter[1], in.sz_max, static_cast<float>(static_cast<int>(in.mode))};
    std::memcpy(blk.jitter, jitter, sizeof jitter);
    std::memcpy(blk.reproj, in.reproject.data(), sizeof blk.reproj);
    const float params[4] = {reset ? 1.0f : 0.0f,
                             in.mode == TemporalMode::Motion ? 0.0f : std::clamp(in.sharpness, 0.0f, 1.0f), 0.0f,
                             tiler_ && temporal && vbytes > 0 ? 1.0f : 0.0f};
    std::memcpy(blk.params, params, sizeof params);
    std::memcpy(slot.mapped, &blk, sizeof blk);
    vmaFlushAllocation(ctx_.allocator, slot.uboAlloc, 0, VK_WHOLE_SIZE);

    vkResetDescriptorPool(ctx_.device, slot.pool, 0);
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = slot.pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &setLayout_;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (VkResult r = vkAllocateDescriptorSets(ctx_.device, &ai, &set); r != VK_SUCCESS)
        return Result<void>::fail(vkFail("descriptor set", r));
    const GpuImage& histIn = history_[1 - current_];
    const GpuImage& histOut = history_[current_];
    const GpuImage& histDepthIn = historyDepth_[1 - current_];
    const GpuImage& histDepthOut = historyDepth_[current_];
    const VkImageLayout ro = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, gen = VK_IMAGE_LAYOUT_GENERAL;
    const VkDescriptorBufferInfo ubo{slot.ubo, 0, sizeof(Block)};
    const VkImageLayout own = tiler_ ? ro : gen;
    const VkDescriptorImageInfo infos[kBindingCount] = {
        {},
        {nearest_, color.sampledView ? color.sampledView : color.view, ro},
        {nearest_, depth.sampledView, ro},
        {nearest_, motionImage_.view, own},
        {linear_, histIn.view, own},
        {nearest_, histDepthIn.view, gen},
        {VK_NULL_HANDLE, histOut.view, gen},
        {VK_NULL_HANDLE, histDepthOut.view, gen},
        {VK_NULL_HANDLE, result_.view, gen},
        {nearest_, histOut.view, own},
        {nearest_, result_.view, gen}};
    std::array<VkWriteDescriptorSet, kBindingCount> w{};
    std::uint32_t writes = 0;
    for (std::uint32_t i = 0; i < kBindingCount; ++i) {
        // The tiler's shaders use none of the images it does not make; unused bindings may stay unwritten.
        if (tiler_ &&
            (i == kHistoryDepthIn || i == kHistoryOut || i == kHistoryDepthOut || i == kResult || i == kResultSampled))
            continue;
        VkWriteDescriptorSet& wi = w[writes++];
        wi = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        wi.dstSet = set;
        wi.dstBinding = i;
        wi.descriptorCount = 1;
        wi.descriptorType = kTypes[i];
        if (i == kUniforms)
            wi.pBufferInfo = &ubo;
        else
            wi.pImageInfo = &infos[i];
    }
    vkUpdateDescriptorSets(ctx_.device, writes, w.data(), 0, nullptr);

    vkn::beginSpan(device_, "temporal");
    vkn::transition(device_, in.depth, ro);
    vkn::transition(device_, in.color, ro);
    if (tiler_) {
        recordTiler(cmd, set, slot, in, color, src, dst, composite);
        vkn::transition(device_, in.color, color.rest);
        historyValid_ = in.mode == TemporalMode::Taa;
        current_ = 1 - current_;
        return Result<void>::ok();
    }
    barrier(cmd);
    if (temporal) {
        const VkClearColorValue zero{};
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, motionImage_.image, gen, &zero, 1, &range);
        if (vbytes > 0) {
            barrier(cmd);
            VkRenderingAttachmentInfo att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            att.imageView = motionImage_.view;
            att.imageLayout = gen;
            att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            VkRenderingAttachmentInfo datt{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            datt.imageView = motionDepth_.view;
            datt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            datt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            datt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            datt.clearValue.depthStencil = {1.0f, 0};
            VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
            const VkRect2D area{{0, 0}, {sw, sh}};
            ri.renderArea = area;
            ri.layerCount = 1;
            ri.colorAttachmentCount = 1;
            ri.pColorAttachments = &att;
            ri.pDepthAttachment = &datt;
            ctx_.rendering->begin(cmd, ri, &kMotionFormat, kMotionDepthFormat);
            // Flipped like the backend's own passes, so NDC +Y is up as the scene drew it.
            const VkViewport viewport{0.0f, src[3], src[2], -src[3], 0.0f, 1.0f};
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &area);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, motion_);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set, 0, nullptr);
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &slot.vbo, &offset);
            vkCmdDraw(cmd, static_cast<std::uint32_t>(in.dynamic.size()), 1, 0, 0);
            ctx_.rendering->end(cmd);
        }
        barrier(cmd);
    }
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 0, 1, &set, 0, nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, temporal ? resolve_ : spatial_);
    vkCmdDispatch(cmd, (dw + 7) / 8, (dh + 7) / 8, 1);
    barrier(cmd);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, sharpen_);
    vkCmdDispatch(cmd, (dw + 7) / 8, (dh + 7) / 8, 1);
    barrier(cmd);

    vkn::transition(device_, in.color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    att.imageView = color.view;
    att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    const VkRect2D area{{static_cast<std::int32_t>(dst[0]), static_cast<std::int32_t>(dst[1])}, {dw, dh}};
    ri.renderArea = area;
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &att;
    ctx_.rendering->begin(cmd, ri, &color.format, VK_FORMAT_UNDEFINED);
    const VkViewport viewport{dst[0], dst[1], dst[2], dst[3], 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &area);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, composite);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    ctx_.rendering->end(cmd);
    vkn::transition(device_, in.color, color.rest);
    vkn::endSpan(device_);

    // Only a temporal frame leaves a history the next one may continue.
    historyValid_ = in.mode == TemporalMode::Taa;
    current_ = 1 - current_;
    return Result<void>::ok();
}

}  // namespace

std::unique_ptr<Temporal> createTemporal(Device& device, std::string* reason, bool tiler) {
    auto say = [&](const char* why) {
        if (reason) *reason = why;
        return nullptr;
    };
    const vkn::Context* ctx = vkn::context(device);
    if (!ctx) return say("needs the native Vulkan renderer");
    if (!(ctx->queueFlags & VK_QUEUE_COMPUTE_BIT)) return say("the graphics queue has no compute");
    if (!formatHas(ctx->physical, kHistoryFormat,
                   VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) ||
        !formatHas(ctx->physical, kHistoryDepthFormat,
                   VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) ||
        !formatHas(ctx->physical, kMotionFormat,
                   VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) ||
        !formatHas(ctx->physical, kMotionDepthFormat, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
        return say("the device cannot store the history");
    // Else the desktop layout, which runs everywhere the checks above pass.
    tiler = tiler &&
            formatHas(ctx->physical, kHistoryFormat,
                      VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) &&
            formatHas(ctx->physical, kTilerMotionFormat,
                      VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    auto t = std::make_unique<VulkanTemporal>(device, *ctx, tiler);
    if (auto r = t->init(); !r) {
        if (reason) *reason = r.error();
        return nullptr;
    }
    if (reason) reason->clear();
    return t;
}

}  // namespace sh::rhi
