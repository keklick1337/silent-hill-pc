// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#include "render/rhi/vk_pipeline_warm.h"

#include <SDL3/SDL.h>

#include <algorithm>

namespace sh::rhi::vkn {

namespace {

constexpr std::uint32_t kRecipeFormat = 1;
constexpr std::uint32_t kMaxAttributes = 16;
constexpr std::uint32_t kMaxEntryBytes = 64;

class Bytes {
public:
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    void u64(std::uint64_t v) {
        u32(static_cast<std::uint32_t>(v));
        u32(static_cast<std::uint32_t>(v >> 32));
    }
    void text(const std::string& s) {
        u32(static_cast<std::uint32_t>(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    }
    std::vector<std::uint8_t> out;
};

class Parse {
public:
    explicit Parse(std::span<const std::uint8_t> in) : in_(in) {}
    std::uint32_t u32() {
        if (in_.size() - at_ < 4) {
            ok = false;
            return 0;
        }
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= std::uint32_t{in_[at_ + i]} << (8 * i);
        at_ += 4;
        return v;
    }
    std::uint64_t u64() {
        const std::uint64_t lo = u32();
        return lo | (std::uint64_t{u32()} << 32);
    }
    std::string text() {
        const std::uint32_t n = u32();
        if (!ok || n > kMaxEntryBytes || in_.size() - at_ < n) {
            ok = false;
            return {};
        }
        std::string s(reinterpret_cast<const char*>(in_.data() + at_), n);
        at_ += n;
        return s;
    }
    bool done() const { return ok && at_ == in_.size(); }
    bool ok = true;

private:
    std::span<const std::uint8_t> in_;
    std::size_t at_ = 0;
};

}  // namespace

// The specialisation constant id of the window's pre-rotation in post.vert, overlay.vert, present.frag and
// hdr_resolve.frag.
constexpr std::uint32_t kPreRotateConstant = 20;

std::vector<std::uint8_t> GraphicsRecipe::encode() const {
    Bytes b;
    b.u32(kRecipeFormat);
    for (std::uint32_t s = 0; s < kStages; ++s) {
        b.u64(shader[s]);
        b.text(shader[s] ? entry[s] : std::string());
    }
    for (std::uint32_t n : sets) b.u32(n);
    b.u32(stride);
    b.u32(static_cast<std::uint32_t>(attributes.size()));
    for (const Attribute& a : attributes) {
        b.u32(a.location);
        b.u32(static_cast<std::uint32_t>(a.format));
        b.u32(a.offset);
    }
    b.u32(static_cast<std::uint32_t>(topology));
    b.u32(patchControlPoints);
    b.u32(wireframe);
    b.u32(cull);
    b.u32(static_cast<std::uint32_t>(frontFace));
    b.u32(depthTest);
    b.u32(depthWrite);
    b.u32(static_cast<std::uint32_t>(depthCompare));
    b.u32(stencilTest);
    b.u32(static_cast<std::uint32_t>(stencil.failOp));
    b.u32(static_cast<std::uint32_t>(stencil.passOp));
    b.u32(static_cast<std::uint32_t>(stencil.depthFailOp));
    b.u32(static_cast<std::uint32_t>(stencil.compareOp));
    b.u32(stencil.compareMask);
    b.u32(stencil.writeMask);
    b.u32(stencil.reference);
    b.u32(blend);
    b.u32(static_cast<std::uint32_t>(srcColor));
    b.u32(static_cast<std::uint32_t>(dstColor));
    b.u32(static_cast<std::uint32_t>(srcAlpha));
    b.u32(static_cast<std::uint32_t>(dstAlpha));
    b.u32(static_cast<std::uint32_t>(colorOp));
    b.u32(static_cast<std::uint32_t>(alphaOp));
    b.u32(writeMask);
    b.u32(static_cast<std::uint32_t>(color));
    b.u32(static_cast<std::uint32_t>(depth));
    if (preRotate) b.u32(preRotate);
    return std::move(b.out);
}

std::optional<GraphicsRecipe> GraphicsRecipe::decode(std::span<const std::uint8_t> bytes) {
    Parse p(bytes);
    if (p.u32() != kRecipeFormat) return std::nullopt;
    GraphicsRecipe r;
    for (std::uint32_t s = 0; s < kStages; ++s) {
        r.shader[s] = p.u64();
        r.entry[s] = p.text();
    }
    for (std::uint32_t& n : r.sets) n = p.u32();
    r.stride = p.u32();
    const std::uint32_t count = p.u32();
    if (!p.ok || count > kMaxAttributes) return std::nullopt;
    r.attributes.resize(count);
    for (Attribute& a : r.attributes) {
        a.location = p.u32();
        a.format = static_cast<VkFormat>(p.u32());
        a.offset = p.u32();
    }
    r.topology = static_cast<VkPrimitiveTopology>(p.u32());
    r.patchControlPoints = p.u32();
    r.wireframe = p.u32() != 0;
    r.cull = p.u32();
    r.frontFace = static_cast<VkFrontFace>(p.u32());
    r.depthTest = p.u32() != 0;
    r.depthWrite = p.u32() != 0;
    r.depthCompare = static_cast<VkCompareOp>(p.u32());
    r.stencilTest = p.u32() != 0;
    r.stencil.failOp = static_cast<VkStencilOp>(p.u32());
    r.stencil.passOp = static_cast<VkStencilOp>(p.u32());
    r.stencil.depthFailOp = static_cast<VkStencilOp>(p.u32());
    r.stencil.compareOp = static_cast<VkCompareOp>(p.u32());
    r.stencil.compareMask = p.u32();
    r.stencil.writeMask = p.u32();
    r.stencil.reference = p.u32();
    r.blend = p.u32() != 0;
    r.srcColor = static_cast<VkBlendFactor>(p.u32());
    r.dstColor = static_cast<VkBlendFactor>(p.u32());
    r.srcAlpha = static_cast<VkBlendFactor>(p.u32());
    r.dstAlpha = static_cast<VkBlendFactor>(p.u32());
    r.colorOp = static_cast<VkBlendOp>(p.u32());
    r.alphaOp = static_cast<VkBlendOp>(p.u32());
    r.writeMask = p.u32();
    r.color = static_cast<VkFormat>(p.u32());
    r.depth = static_cast<VkFormat>(p.u32());
    if (p.ok && !p.done()) r.preRotate = p.u32();
    if (!p.done() || !r.shader[kFragment] || (!r.shader[kVertex] && !r.shader[kMesh])) return std::nullopt;
    return r;
}

std::vector<std::uint64_t> GraphicsRecipe::shaders() const {
    std::vector<std::uint64_t> out;
    for (std::uint64_t s : shader)
        if (s) out.push_back(s);
    return out;
}

VkResult buildGraphicsPipeline(VkDevice device, VkPipelineCache cache, const GraphicsRecipe& r,
                               const std::array<VkShaderModule, GraphicsRecipe::kStages>& modules,
                               const BuildTarget& target, bool nonSolidFill, VkPipeline* out) {
    static constexpr VkShaderStageFlagBits kStageBits[GraphicsRecipe::kStages] = {
        VK_SHADER_STAGE_TASK_BIT_EXT,
        VK_SHADER_STAGE_MESH_BIT_EXT,
        VK_SHADER_STAGE_VERTEX_BIT,
        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
        VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
        VK_SHADER_STAGE_FRAGMENT_BIT};
    VkPipelineShaderStageCreateInfo stages[GraphicsRecipe::kStages]{};
    std::uint32_t stageCount = 0;
    for (std::uint32_t s = 0; s < GraphicsRecipe::kStages; ++s) {
        if (!modules[s]) continue;
        VkPipelineShaderStageCreateInfo& st = stages[stageCount++];
        st.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        st.stage = kStageBits[s];
        st.module = modules[s];
        st.pName = r.entry[s].c_str();
    }
    const std::uint32_t turn = r.preRotate;
    const VkSpecializationMapEntry turnEntry{kPreRotateConstant, 0, sizeof(turn)};
    const VkSpecializationInfo turnInfo{1, &turnEntry, sizeof(turn), &turn};
    if (turn) {
        for (std::uint32_t i = 0; i < stageCount; ++i)
            if (stages[i].stage == VK_SHADER_STAGE_VERTEX_BIT || stages[i].stage == VK_SHADER_STAGE_FRAGMENT_BIT)
                stages[i].pSpecializationInfo = &turnInfo;
    }
    const bool mesh = modules[GraphicsRecipe::kMesh] != VK_NULL_HANDLE;
    const bool tess = modules[GraphicsRecipe::kTessEval] != VK_NULL_HANDLE;

    VkVertexInputBindingDescription binding{0, r.stride, VK_VERTEX_INPUT_RATE_VERTEX};
    std::vector<VkVertexInputAttributeDescription> attrs;
    for (const GraphicsRecipe::Attribute& a : r.attributes) attrs.push_back({a.location, 0, a.format, a.offset});
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = r.stride ? 1 : 0;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attrs.size());
    vi.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = tess ? VK_PRIMITIVE_TOPOLOGY_PATCH_LIST : r.topology;
    // GL's domain origin, so a patch's generated triangles keep the winding of its control points.
    VkPipelineTessellationDomainOriginStateCreateInfo origin{
        VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_DOMAIN_ORIGIN_STATE_CREATE_INFO};
    origin.domainOrigin = VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT;
    VkPipelineTessellationStateCreateInfo ts{VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO};
    ts.pNext = &origin;
    ts.patchControlPoints = r.patchControlPoints;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = r.wireframe && nonSolidFill ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rs.cullMode = r.cull;
    rs.frontFace = r.frontFace;
    rs.lineWidth = 1.f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = r.depthTest;
    ds.depthWriteEnable = r.depthWrite;
    ds.depthCompareOp = r.depthCompare;
    ds.stencilTestEnable = r.stencilTest;
    ds.front = r.stencil;
    ds.back = r.stencil;
    ds.minDepthBounds = 0.f;
    ds.maxDepthBounds = 1.f;
    VkPipelineColorBlendAttachmentState att{};
    att.blendEnable = r.blend;
    att.srcColorBlendFactor = r.srcColor;
    att.dstColorBlendFactor = r.dstColor;
    att.colorBlendOp = r.colorOp;
    att.srcAlphaBlendFactor = r.srcAlpha;
    att.dstAlphaBlendFactor = r.dstAlpha;
    att.alphaBlendOp = r.alphaOp;
    att.colorWriteMask = r.writeMask & 0xF;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &att;
    const VkDynamicState dynamic[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                      VK_DYNAMIC_STATE_BLEND_CONSTANTS, VK_DYNAMIC_STATE_STENCIL_REFERENCE};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 4;
    dy.pDynamicStates = dynamic;
    const bool stencilFormat = r.depth == VK_FORMAT_D24_UNORM_S8_UINT || r.depth == VK_FORMAT_D32_SFLOAT_S8_UINT ||
                               r.depth == VK_FORMAT_D16_UNORM_S8_UINT;
    VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ri.colorAttachmentCount = 1;
    ri.pColorAttachmentFormats = &r.color;
    ri.depthAttachmentFormat = r.depth;
    ri.stencilAttachmentFormat = stencilFormat ? r.depth : VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    if (target.renderPass) {
        info.renderPass = target.renderPass;
        info.subpass = 0;
    } else {
        info.pNext = &ri;
    }
    info.stageCount = stageCount;
    info.pStages = stages;
    info.pVertexInputState = mesh ? nullptr : &vi;
    info.pInputAssemblyState = mesh ? nullptr : &ia;
    info.pTessellationState = tess ? &ts : nullptr;
    info.pViewportState = &vp;
    info.pRasterizationState = &rs;
    info.pMultisampleState = &ms;
    info.pDepthStencilState = &ds;
    info.pColorBlendState = &cb;
    info.pDynamicState = &dy;
    info.layout = target.layout;
    return vkCreateGraphicsPipelines(device, cache, 1, &info, nullptr, out);
}

void PipelineWarmer::start(VkDevice device, VkPipelineCache cache, bool nonSolidFill, unsigned threads) {
    stop();
    device_ = device;
    cache_ = cache;
    nonSolidFill_ = nonSolidFill;
    stopping_ = false;
    for (unsigned i = 0; i < std::max(threads, 1u); ++i) workers_.emplace_back([this] { run(); });
}

void PipelineWarmer::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        jobs_.clear();
        tasks_.clear();
    }
    wake_.notify_all();
    for (std::thread& t : workers_) t.join();
    workers_.clear();
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [key, e] : entries_) {
        if (e.state == State::Built && e.pipeline) vkDestroyPipeline(device_, e.pipeline, nullptr);
        if (e.state != State::Taken) e.state = State::Taken;
        e.pipeline = VK_NULL_HANDLE;
    }
    built_.notify_all();
}

bool PipelineWarmer::add(Job job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (workers_.empty() || stopping_ || entries_.contains(job.key)) return false;
        entries_[job.key] = {};
        ++progress_.queued;
        jobs_.push_back(std::move(job));
    }
    wake_.notify_one();
    return true;
}

bool PipelineWarmer::known(std::uint64_t key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.contains(key);
}

VkPipeline PipelineWarmer::take(std::uint64_t key, double* waitedMs) {
    *waitedMs = 0.0;
    std::unique_lock<std::mutex> lock(mutex_);
    const auto it = entries_.find(key);
    if (it == entries_.end()) return VK_NULL_HANDLE;
    Entry& e = it->second;
    if (e.state == State::Queued) {
        // The caller builds it now; the worker that pops the job skips it.
        e.state = State::Taken;
        ++progress_.finished;
        return VK_NULL_HANDLE;
    }
    if (e.state == State::Building) {
        const Uint64 from = SDL_GetPerformanceCounter();
        built_.wait(lock, [&] { return e.state != State::Building; });
        *waitedMs = static_cast<double>(SDL_GetPerformanceCounter() - from) * 1000.0 /
                    static_cast<double>(SDL_GetPerformanceFrequency());
    }
    const VkPipeline p = e.state == State::Built ? e.pipeline : VK_NULL_HANDLE;
    e.state = State::Taken;
    e.pipeline = VK_NULL_HANDLE;
    return p;
}

void PipelineWarmer::post(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (workers_.empty() || stopping_) return;
        tasks_.push_back(std::move(task));
    }
    wake_.notify_one();
}

PipelineWarmer::Progress PipelineWarmer::progress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return progress_;
}

void PipelineWarmer::run() {
    for (;;) {
        Job job;
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [&] { return stopping_ || !jobs_.empty() || !tasks_.empty(); });
            if (stopping_) return;
            if (!jobs_.empty()) {
                job = std::move(jobs_.front());
                jobs_.pop_front();
                Entry& e = entries_[job.key];
                if (e.state != State::Queued) continue;  // the device built it itself
                e.state = State::Building;
            } else {
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
        }
        if (task) {
            task();
            continue;
        }
        const Uint64 from = SDL_GetPerformanceCounter();
        std::array<VkShaderModule, GraphicsRecipe::kStages> modules{};
        bool ok = true;
        for (std::uint32_t s = 0; s < GraphicsRecipe::kStages && ok; ++s) {
            if (!job.recipe.shader[s]) continue;
            if (!job.code[s]) {
                ok = false;
                break;
            }
            VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            info.codeSize = job.code[s]->size() * sizeof(std::uint32_t);
            info.pCode = job.code[s]->data();
            ok = vkCreateShaderModule(device_, &info, nullptr, &modules[s]) == VK_SUCCESS;
        }
        VkPipeline pipeline = VK_NULL_HANDLE;
        if (ok)
            ok = buildGraphicsPipeline(device_, cache_, job.recipe, modules, job.target, nonSolidFill_, &pipeline) ==
                 VK_SUCCESS;
        for (VkShaderModule m : modules)
            if (m) vkDestroyShaderModule(device_, m, nullptr);
        const double ms = static_cast<double>(SDL_GetPerformanceCounter() - from) * 1000.0 /
                          static_cast<double>(SDL_GetPerformanceFrequency());
        {
            std::lock_guard<std::mutex> lock(mutex_);
            Entry& e = entries_[job.key];
            e.state = State::Built;
            e.pipeline = ok ? pipeline : VK_NULL_HANDLE;
            ++progress_.finished;
            if (!ok) ++progress_.failed;
            progress_.buildMs += ms;
        }
        built_.notify_all();
    }
}

}  // namespace sh::rhi::vkn
