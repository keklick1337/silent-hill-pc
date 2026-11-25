// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#include "render/rhi/vk_atmosphere.h"

#include "render/rhi/rhi_vulkan_native.h"
#include "render/rhi/vk_rendering.h"
#include "render/rhi/shaders/embedded.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace sh::rhi::embedded {
Shader atmosphereAoComp();
Shader atmosphereInjectComp();
Shader atmosphereIntegrateComp();
Shader atmosphereCompositeVert();
Shader atmosphereCompositeFrag();
}  // namespace sh::rhi::embedded

namespace sh::rhi {

namespace {

constexpr std::uint32_t kSlices = 96;
// The volume is about one froxel per ten pixels, capped so a 4K frame stays near 16 MB per volume.
constexpr std::uint32_t kFroxelPixels = 10;
// A phone's GPU: a coarser volume (a 4K desktop frame's froxels are 20 pixels wide under the cap; the fog and the
// beam are smooth at that scale) and fewer occlusion taps per estimate; about a quarter of the fog's froxels.
constexpr std::uint32_t kPhoneSlices = 64;
constexpr std::uint32_t kPhoneFroxelPixels = 16;
constexpr std::int32_t kPhoneAoTaps = 8;
static_assert(kSlices <= 128 && kPhoneSlices <= 128, "integrate.comp's kMaxSlices");
constexpr std::uint32_t kMaxGridX = 192;
constexpr std::uint32_t kMaxGridY = 108;
constexpr float kFirstSlice = 48.0f;  // view units: the first, linear slice ends here
// The haze the beam scatters in where the map has no fog: 5 % per metre (256 view units), lighting it only.
constexpr float kHazePerUnit = 0.05f / 256.0f;

constexpr VkFormat kVolumeFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkFormat kAoFormat = VK_FORMAT_R32_SFLOAT;

enum Binding : std::uint32_t {
    kUniforms = 0,
    kDepth = 1,
    kShadow = 2,
    kAo = 3,
    kInject = 4,
    kVolume = 5,
    kVolumeSampled = 6,
    kSunShadow = 7,
    kBindingCount = 8
};

// std140 Atmosphere block of shaders/atmosphere/common.glsl.
struct Block {
    float proj[4];
    float view[4];
    float depth[4];
    float grid[4];
    float fog[4];
    float fogRampP[4];
    float fogRamp[128];
    float flMode[4];
    float flPos[4];
    float flDir[4];
    float flColor[4];
    float shadowMatrix[16];
    float shadowQ[4];
    float ao[4];
    float scatter[4];
    float lightInfo[4];
    float lightPos[kAtmosphereLights][4];
    float lightColor[kAtmosphereLights][4];
    float sunDir[4];
    float sunColor[4];
    float sunMatrix[16];
};
static_assert(sizeof(Block) == 4 * (4 * 6 + 128 + 4 * 4 + 16 + 4 * 3 + 4 + 8 * kAtmosphereLights + 4 * 2 + 16));

struct GpuImage {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation alloc = nullptr;
    std::uint32_t w = 0, h = 0, d = 0;
};

struct Slot {
    VkBuffer ubo = VK_NULL_HANDLE;
    VmaAllocation uboAlloc = nullptr;
    void* mapped = nullptr;
    VkDescriptorPool pool = VK_NULL_HANDLE;
};

std::string vkFail(const char* what, VkResult r) {
    return std::string("atmosphere: ") + what + " (VkResult " + std::to_string(r) + ")";
}

bool formatHas(VkPhysicalDevice physical, VkFormat format, VkFormatFeatureFlags need) {
    VkFormatProperties p{};
    vkGetPhysicalDeviceFormatProperties(physical, format, &p);
    return (p.optimalTilingFeatures & need) == need;
}

class VulkanAtmosphere final : public Atmosphere {
public:
    VulkanAtmosphere(Device& device, const vkn::Context& ctx, bool phone)
        : device_(device),
          ctx_(ctx),
          phone_(phone),
          slices_(phone ? kPhoneSlices : kSlices),
          froxelPixels_(phone ? kPhoneFroxelPixels : kFroxelPixels) {}
    ~VulkanAtmosphere() override;

    Result<void> init();
    Result<void> apply(const AtmosphereInputs& in) override;

private:
    VkShaderModule module(const embedded::Shader& s);
    Result<void> makeImage(GpuImage& img, VkImageType type, VkFormat format, std::uint32_t w, std::uint32_t h,
                           std::uint32_t d, VkImageUsageFlags usage);
    void releaseImage(GpuImage& img);
    Result<void> ensureImages(VkCommandBuffer cmd, std::uint32_t aoW, std::uint32_t aoH, std::uint32_t gx,
                              std::uint32_t gy);
    VkPipeline compositePipeline(VkFormat color);
    void computeBarrier(VkCommandBuffer cmd, VkPipelineStageFlags2 dst);

    Device& device_;
    vkn::Context ctx_;
    // The shaders' kPhone and kTaps specialisation constants; left at their defaults off a phone.
    bool phone_;
    std::uint32_t slices_;
    std::uint32_t froxelPixels_;
    struct PhoneConstants {
        VkBool32 phone = VK_TRUE;
        std::int32_t aoTaps = kPhoneAoTaps;
    } phoneConstants_;
    std::array<VkSpecializationMapEntry, 2> phoneEntries_{
        {{0, offsetof(PhoneConstants, phone), sizeof(VkBool32)},
         {1, offsetof(PhoneConstants, aoTaps), sizeof(std::int32_t)}}};
    VkSpecializationInfo phoneSpec_{2, phoneEntries_.data(), sizeof(PhoneConstants), &phoneConstants_};
    const VkSpecializationInfo* spec() const { return phone_ ? &phoneSpec_ : nullptr; }
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline ao_ = VK_NULL_HANDLE, inject_ = VK_NULL_HANDLE, integrate_ = VK_NULL_HANDLE;
    VkShaderModule compositeVs_ = VK_NULL_HANDLE, compositeFs_ = VK_NULL_HANDLE;
    struct Variant {
        VkFormat color;
        VkPipeline pipeline;
    };
    std::vector<Variant> composites_;
    VkSampler nearest_ = VK_NULL_HANDLE, linear_ = VK_NULL_HANDLE;
    std::vector<Slot> slots_;
    GpuImage aoImage_, injectImage_, volumeImage_;
    std::uint64_t appliedSerial_ = 0;
};

VulkanAtmosphere::~VulkanAtmosphere() {
    VkDevice dev = ctx_.device;
    VmaAllocator vma = ctx_.allocator;
    // The device may still run the last frames that used these: release them with that frame's other garbage.
    std::vector<VkPipeline> pipes{ao_, inject_, integrate_};
    for (const Variant& v : composites_) pipes.push_back(v.pipeline);
    std::vector<GpuImage> images{aoImage_, injectImage_, volumeImage_};
    std::vector<Slot> slots = slots_;
    VkShaderModule vs = compositeVs_, fs = compositeFs_;
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
        }
        if (vs) vkDestroyShaderModule(dev, vs, nullptr);
        if (fs) vkDestroyShaderModule(dev, fs, nullptr);
        if (nearest) vkDestroySampler(dev, nearest, nullptr);
        if (linear) vkDestroySampler(dev, linear, nullptr);
        if (layout) vkDestroyPipelineLayout(dev, layout, nullptr);
        if (setLayout) vkDestroyDescriptorSetLayout(dev, setLayout, nullptr);
    });
}

VkShaderModule VulkanAtmosphere::module(const embedded::Shader& s) {
    if (s.spirv.empty()) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = s.spirv.size_bytes();
    ci.pCode = s.spirv.data();
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(ctx_.device, &ci, nullptr, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
    return m;
}

Result<void> VulkanAtmosphere::init() {
    VkDevice dev = ctx_.device;
    std::array<VkDescriptorSetLayoutBinding, kBindingCount> b{};
    const VkDescriptorType types[kBindingCount] = {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
    for (std::uint32_t i = 0; i < kBindingCount; ++i) {
        b[i].binding = i;
        b[i].descriptorType = types[i];
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
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
        ci.stage.pSpecializationInfo = spec();
        ci.layout = layout_;
        const VkResult r = vkCreateComputePipelines(dev, ctx_.pipelineCache, 1, &ci, nullptr, out);
        vkDestroyShaderModule(dev, m, nullptr);
        return r == VK_SUCCESS;
    };
    if (!compute(embedded::atmosphereAoComp(), &ao_) || !compute(embedded::atmosphereInjectComp(), &inject_) ||
        !compute(embedded::atmosphereIntegrateComp(), &integrate_))
        return Result<void>::fail("atmosphere: the compute shaders are missing or were refused");
    compositeVs_ = module(embedded::atmosphereCompositeVert());
    compositeFs_ = module(embedded::atmosphereCompositeFrag());
    if (!compositeVs_ || !compositeFs_) return Result<void>::fail("atmosphere: the composite shaders are missing");

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
                                              {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
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

Result<void> VulkanAtmosphere::makeImage(GpuImage& img, VkImageType type, VkFormat format, std::uint32_t w,
                                         std::uint32_t h, std::uint32_t d, VkImageUsageFlags usage) {
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = type;
    ci.format = format;
    ci.extent = {w, h, d};
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
    vi.viewType = type == VK_IMAGE_TYPE_3D ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (VkResult r = vkCreateImageView(ctx_.device, &vi, nullptr, &img.view); r != VK_SUCCESS)
        return Result<void>::fail(vkFail("image view", r));
    img.w = w;
    img.h = h;
    img.d = d;
    return Result<void>::ok();
}

void VulkanAtmosphere::releaseImage(GpuImage& img) {
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

Result<void> VulkanAtmosphere::ensureImages(VkCommandBuffer cmd, std::uint32_t aoW, std::uint32_t aoH, std::uint32_t gx,
                                            std::uint32_t gy) {
    std::vector<VkImage> fresh;
    auto want = [&](GpuImage& img, VkImageType type, VkFormat format, std::uint32_t w, std::uint32_t h, std::uint32_t d,
                    VkImageUsageFlags usage) -> Result<void> {
        if (img.image && img.w == w && img.h == h && img.d == d) return Result<void>::ok();
        releaseImage(img);
        if (auto r = makeImage(img, type, format, w, h, d, usage); !r) return r;
        fresh.push_back(img.image);
        return Result<void>::ok();
    };
    if (auto r = want(aoImage_, VK_IMAGE_TYPE_2D, kAoFormat, aoW, aoH, 1, VK_IMAGE_USAGE_STORAGE_BIT); !r) return r;
    if (auto r = want(injectImage_, VK_IMAGE_TYPE_3D, kVolumeFormat, gx, gy, slices_, VK_IMAGE_USAGE_STORAGE_BIT); !r)
        return r;
    if (auto r = want(volumeImage_, VK_IMAGE_TYPE_3D, kVolumeFormat, gx, gy, slices_ + 1,
                      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        !r)
        return r;
    // Own images live in the general layout for their whole life.
    std::vector<VkImageMemoryBarrier2> barriers;
    for (VkImage image : fresh) {
        VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barriers.push_back(b);
    }
    if (!barriers.empty()) {
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = static_cast<std::uint32_t>(barriers.size());
        dep.pImageMemoryBarriers = barriers.data();
        ctx_.rendering->barrier(cmd, dep);
    }
    return Result<void>::ok();
}

VkPipeline VulkanAtmosphere::compositePipeline(VkFormat color) {
    for (const Variant& v : composites_)
        if (v.color == color) return v.pipeline;
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = compositeVs_;
    stages[0].pName = "main";
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = compositeFs_;
    stages[1].pName = "main";
    stages[1].pSpecializationInfo = spec();
    VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
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
    // dst' = src + dst * src.a; the target's alpha is kept.
    VkPipelineColorBlendAttachmentState att{};
    att.blendEnable = VK_TRUE;
    att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    att.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    att.colorBlendOp = VK_BLEND_OP_ADD;
    att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    att.alphaBlendOp = VK_BLEND_OP_ADD;
    att.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
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
    composites_.push_back({color, p});
    return p;
}

void VulkanAtmosphere::computeBarrier(VkCommandBuffer cmd, VkPipelineStageFlags2 dst) {
    VkMemoryBarrier2 b{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    b.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    b.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    b.dstStageMask = dst;
    b.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT |
                      VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &b;
    ctx_.rendering->barrier(cmd, dep);
}

// Where the fog hides everything along the view axis: the last slice ends there. Past the ramp's end the fog is
// complete; a cull distance ends it sooner.
float fogEnd(const FogRampUniforms& fog) {
    float end = fog.params[1] > 0.0f ? 128.0f / fog.params[1] : 32768.0f;
    if (fog.params[3] > 0.0f) end = std::min(end, fog.params[3]);
    return std::max(end, kFirstSlice * 2.0f);
}

Result<void> VulkanAtmosphere::apply(const AtmosphereInputs& in) {
    if (!in.ao && !in.volumetric) return Result<void>::ok();
    const std::uint64_t serial = vkn::frameSerial(device_);
    if (serial == appliedSerial_) return Result<void>::ok();
    vkn::Image color, depth, shadow;
    if (!vkn::image(device_, in.color, &color) || !vkn::image(device_, in.depth, &depth))
        return Result<void>::fail("atmosphere: no scene target");
    if (!(depth.aspect & VK_IMAGE_ASPECT_DEPTH_BIT) || depth.rest != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        return Result<void>::fail("atmosphere: the scene depth is not sampled");
    const bool shadowed = in.shadow_map && vkn::image(device_, in.shadow_map, &shadow) &&
                          shadow.rest == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkn::Image sunMap;
    const bool sunLit = in.volumetric && in.sun && in.sun_map && vkn::image(device_, in.sun_map, &sunMap) &&
                        sunMap.rest == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const float vx = std::max(0.0f, std::floor(in.viewport_x)), vy = std::max(0.0f, std::floor(in.viewport_y));
    const float vw = std::min(std::floor(in.viewport_w), static_cast<float>(color.width) - vx);
    const float vh = std::min(std::floor(in.viewport_h), static_cast<float>(color.height) - vy);
    if (vw < 8.0f || vh < 8.0f) return Result<void>::ok();

    VkCommandBuffer cmd = vkn::record(device_);
    if (!cmd) return Result<void>::fail("atmosphere: no frame is being recorded");
    appliedSerial_ = serial;
    const std::uint32_t aoStep = static_cast<std::uint32_t>(std::clamp(in.ao_step, 1, 4));
    const auto aoW = in.ao ? (static_cast<std::uint32_t>(vw) + aoStep - 1) / aoStep : 1u;
    const auto aoH = in.ao ? (static_cast<std::uint32_t>(vh) + aoStep - 1) / aoStep : 1u;
    const std::uint32_t gx =
        in.volumetric ? std::clamp(static_cast<std::uint32_t>(vw) / froxelPixels_, 16u, kMaxGridX) : 1;
    const std::uint32_t gy =
        in.volumetric ? std::clamp(static_cast<std::uint32_t>(vh) / froxelPixels_, 9u, kMaxGridY) : 1;
    if (auto r = ensureImages(cmd, aoW, aoH, gx, gy); !r) return r;
    const VkPipeline composite = compositePipeline(color.format);
    if (!composite) return Result<void>::fail("atmosphere: no composite pipeline for the scene format");

    Block blk{};
    std::memcpy(blk.proj, in.proj.data(), sizeof blk.proj);
    const float view[4] = {vx, vy, vw, vh};
    std::memcpy(blk.view, view, sizeof view);
    const float depthP[4] = {in.sz_max, kFirstSlice, fogEnd(in.fog), static_cast<float>(slices_)};
    std::memcpy(blk.depth, depthP, sizeof depthP);
    const float grid[4] = {static_cast<float>(gx), static_cast<float>(gy), static_cast<float>(slices_ + 1),
                           in.ao ? static_cast<float>(aoStep) : 0.0f};
    std::memcpy(blk.grid, grid, sizeof grid);
    std::memcpy(blk.fog, in.fs.fog.data(), sizeof blk.fog);
    std::memcpy(blk.fogRampP, in.fog.params.data(), sizeof blk.fogRampP);
    std::memcpy(blk.fogRamp, in.fog.ramp.data(), sizeof blk.fogRamp);
    std::memcpy(blk.flMode, in.fs.flMode.data(), sizeof blk.flMode);
    std::memcpy(blk.flPos, in.fs.flPos.data(), sizeof blk.flPos);
    std::memcpy(blk.flDir, in.fs.flDir.data(), sizeof blk.flDir);
    std::memcpy(blk.flColor, in.fs.flColor.data(), sizeof blk.flColor);
    std::memcpy(blk.shadowMatrix, in.fs.shadowMatrix.data(), sizeof blk.shadowMatrix);
    std::memcpy(blk.shadowQ, in.fs.shadowQ.data(), sizeof blk.shadowQ);
    // Pixels per unit of x / z: NDC per unit times half the viewport.
    const float ao[4] = {std::clamp(in.ao_strength, 0.0f, 1.0f), std::max(in.ao_radius, 1.0f),
                         std::abs(in.proj[0]) * vw * 0.5f, in.volumetric ? 1.0f : 0.0f};
    std::memcpy(blk.ao, ao, sizeof ao);
    const float scatter[4] = {std::clamp(in.scatter, 0.0f, 1.0f), std::clamp(in.anisotropy, -0.9f, 0.9f), kHazePerUnit,
                              shadowed ? 1.0f : 0.0f};
    std::memcpy(blk.scatter, scatter, sizeof scatter);
    const int lights = std::clamp(in.lights, 0, kAtmosphereLights);
    blk.lightInfo[0] = static_cast<float>(lights);
    for (int i = 0; i < lights; ++i) {
        std::memcpy(blk.lightPos[i], in.lightPos[static_cast<std::size_t>(i)].data(), sizeof blk.lightPos[i]);
        std::memcpy(blk.lightColor[i], in.lightColor[static_cast<std::size_t>(i)].data(), sizeof blk.lightColor[i]);
    }
    if (sunLit) {
        std::memcpy(blk.sunDir, in.sunDir.data(), sizeof blk.sunDir);
        std::memcpy(blk.sunColor, in.sunColor.data(), sizeof blk.sunColor);
        blk.sunColor[3] = 1.0f;
        std::memcpy(blk.sunMatrix, in.sunMatrix.data(), sizeof blk.sunMatrix);
    }

    Slot& slot = slots_[vkn::frameSlot(device_) % slots_.size()];
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

    VkDescriptorBufferInfo ubo{slot.ubo, 0, sizeof(Block)};
    const VkDescriptorImageInfo depthInfo{nearest_, depth.sampledView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    // Without a shadow map the slot reads the depth, never used (u_scatter.w = 0).
    const VkDescriptorImageInfo shadowInfo{nearest_, shadowed ? shadow.sampledView : depth.sampledView,
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo sunInfo{nearest_, sunLit ? sunMap.sampledView : depth.sampledView,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo aoInfo{VK_NULL_HANDLE, aoImage_.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo injectInfo{VK_NULL_HANDLE, injectImage_.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo volumeInfo{VK_NULL_HANDLE, volumeImage_.view, VK_IMAGE_LAYOUT_GENERAL};
    const VkDescriptorImageInfo volumeSampled{linear_, volumeImage_.view, VK_IMAGE_LAYOUT_GENERAL};
    std::array<VkWriteDescriptorSet, kBindingCount> w{};
    const VkDescriptorImageInfo* images[kBindingCount] = {nullptr,     &depthInfo,  &shadowInfo,    &aoInfo,
                                                          &injectInfo, &volumeInfo, &volumeSampled, &sunInfo};
    const VkDescriptorType types[kBindingCount] = {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
    for (std::uint32_t i = 0; i < kBindingCount; ++i) {
        w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w[i].dstSet = set;
        w[i].dstBinding = i;
        w[i].descriptorCount = 1;
        w[i].descriptorType = types[i];
        if (i == kUniforms)
            w[i].pBufferInfo = &ubo;
        else
            w[i].pImageInfo = images[i];
    }
    vkUpdateDescriptorSets(ctx_.device, kBindingCount, w.data(), 0, nullptr);

    vkn::beginSpan(device_, "atmosphere");
    vkn::transition(device_, in.depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (shadowed) vkn::transition(device_, in.shadow_map, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (sunLit) vkn::transition(device_, in.sun_map, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout_, 0, 1, &set, 0, nullptr);
    if (in.ao) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ao_);
        vkCmdDispatch(cmd, (aoW + 7) / 8, (aoH + 7) / 8, 1);
    }
    if (in.volumetric) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, inject_);
        vkCmdDispatch(cmd, (gx + 3) / 4, (gy + 3) / 4, (slices_ + 3) / 4);
        computeBarrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, integrate_);
        vkCmdDispatch(cmd, (gx + 7) / 8, (gy + 7) / 8, 1);
    }
    computeBarrier(cmd, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

    vkn::transition(device_, in.color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo att{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    att.imageView = color.view;
    att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    const VkRect2D area{{static_cast<std::int32_t>(vx), static_cast<std::int32_t>(vy)},
                        {static_cast<std::uint32_t>(vw), static_cast<std::uint32_t>(vh)}};
    ri.renderArea = area;
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &att;
    ctx_.rendering->begin(cmd, ri, &color.format, VK_FORMAT_UNDEFINED);
    // Not flipped: gl_FragCoord is the target's own pixel, top row first.
    const VkViewport viewport{vx, vy, vw, vh, 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &area);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, composite);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    ctx_.rendering->end(cmd);
    vkn::transition(device_, in.color, color.rest);
    vkn::endSpan(device_);
    return Result<void>::ok();
}

}  // namespace

std::unique_ptr<Atmosphere> createAtmosphere(Device& device, std::string* reason, bool phone) {
    auto say = [&](const char* why) {
        if (reason) *reason = why;
        return nullptr;
    };
    const vkn::Context* ctx = vkn::context(device);
    if (!ctx) return say("needs the native Vulkan renderer");
    if (!(ctx->queueFlags & VK_QUEUE_COMPUTE_BIT)) return say("the graphics queue has no compute");
    if (!formatHas(ctx->physical, kVolumeFormat,
                   VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) ||
        !formatHas(ctx->physical, kAoFormat, VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
        return say("the device cannot store the fog volume");
    auto a = std::make_unique<VulkanAtmosphere>(device, *ctx, phone);
    if (auto r = a->init(); !r) {
        if (reason) *reason = r.error();
        return nullptr;
    }
    if (reason) reason->clear();
    return a;
}

}  // namespace sh::rhi
