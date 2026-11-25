// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Graphics pipelines of the native Vulkan backend built before their first use (RENDER.md section 2.2d). A recipe is
// everything vkCreateGraphicsPipelines reads besides the layout and the render pass, with the shaders named by the
// hash of their SPIR-V, so it can be written into the manifest and built again at the next start without the code
// that first asked for it. Worker threads build the queued recipes into the device's pipeline cache (a VkPipelineCache
// is internally synchronised unless made with the externally synchronised flag); the device takes a built pipeline
// when the game creates the same recipe. Only code built against the vendored Vulkan headers includes this.
#pragma once

#include <volk.h>

#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace sh::rhi::vkn {

using SpirvCode = std::shared_ptr<const std::vector<std::uint32_t>>;

struct GraphicsRecipe {
    enum Stage : std::uint32_t { kTask, kMesh, kVertex, kTessControl, kTessEval, kFragment, kStages };
    struct Attribute {
        std::uint32_t location = 0;
        VkFormat format = VK_FORMAT_UNDEFINED;
        std::uint32_t offset = 0;
    };
    std::array<std::uint64_t, kStages> shader{};  // pipeline_store::hash64 of the SPIR-V; 0 = no such stage
    std::array<std::string, kStages> entry{};
    std::array<std::uint32_t, 5> sets{};  // descriptor counts of the backend's five sets
    std::uint32_t stride = 0;             // 0 = no vertex buffer
    std::vector<Attribute> attributes;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    std::uint32_t patchControlPoints = 3;
    bool wireframe = false;  // line polygons where the device has fillModeNonSolid
    VkCullModeFlags cull = VK_CULL_MODE_NONE;
    VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    bool depthTest = false;
    bool depthWrite = false;
    VkCompareOp depthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;
    bool stencilTest = false;
    VkStencilOpState stencil{};  // both faces
    bool blend = false;
    VkBlendFactor srcColor = VK_BLEND_FACTOR_ONE, dstColor = VK_BLEND_FACTOR_ZERO;
    VkBlendFactor srcAlpha = VK_BLEND_FACTOR_ONE, dstAlpha = VK_BLEND_FACTOR_ZERO;
    VkBlendOp colorOp = VK_BLEND_OP_ADD, alphaOp = VK_BLEND_OP_ADD;
    std::uint32_t writeMask = 0xF;
    VkFormat color = VK_FORMAT_UNDEFINED;
    VkFormat depth = VK_FORMAT_UNDEFINED;
    // Drawn into a turned swapchain image: the specialisation constant kPreRotate (id 20) of the vertex and fragment
    // stages, 1 or 3 quarter turns. Encoded only when set, so the recipes without it keep their bytes and keys.
    std::uint32_t preRotate = 0;

    std::vector<std::uint8_t> encode() const;
    // Nothing for bytes no encode() wrote.
    static std::optional<GraphicsRecipe> decode(std::span<const std::uint8_t> bytes);
    // The hashes of the stages it has.
    std::vector<std::uint64_t> shaders() const;
};

// What the device resolves on its own thread before a build: its layout and render pass caches are not shared.
struct BuildTarget {
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;  // VK_NULL_HANDLE: dynamic rendering
};

// One vkCreateGraphicsPipelines call of a recipe; `modules` per GraphicsRecipe::Stage (null where it has none).
VkResult buildGraphicsPipeline(VkDevice device, VkPipelineCache cache, const GraphicsRecipe& recipe,
                               const std::array<VkShaderModule, GraphicsRecipe::kStages>& modules,
                               const BuildTarget& target, bool nonSolidFill, VkPipeline* out);

class PipelineWarmer {
public:
    struct Job {
        std::uint64_t key = 0;  // pipeline_store::hash64 of recipe.encode()
        GraphicsRecipe recipe;
        std::array<SpirvCode, GraphicsRecipe::kStages> code{};
        BuildTarget target;
    };
    struct Progress {
        std::uint32_t queued = 0;    // jobs ever added
        std::uint32_t finished = 0;  // built, failed, or taken over by the device before a worker began them
        std::uint32_t failed = 0;
        double buildMs = 0.0;  // summed over the workers
    };

    PipelineWarmer() = default;
    ~PipelineWarmer() { stop(); }
    PipelineWarmer(const PipelineWarmer&) = delete;
    PipelineWarmer& operator=(const PipelineWarmer&) = delete;

    void start(VkDevice device, VkPipelineCache cache, bool nonSolidFill, unsigned threads);
    // Drops the jobs and tasks not begun, joins the workers and destroys the pipelines nobody took.
    void stop();
    bool running() const { return !workers_.empty(); }
    unsigned threads() const { return static_cast<unsigned>(workers_.size()); }

    // False when the key is known already (queued, built or taken).
    bool add(Job job);
    bool known(std::uint64_t key) const;
    // The pipeline a worker built for `key`, now the caller's; waits while one is building it. VK_NULL_HANDLE when
    // there is none: the job, if still queued, is then the caller's to build and the workers skip it.
    // `waitedMs` gets the time spent waiting.
    VkPipeline take(std::uint64_t key, double* waitedMs);
    // Runs `task` on a worker after the jobs queued before it.
    void post(std::function<void()> task);
    Progress progress() const;

private:
    enum class State : std::uint8_t { Queued, Building, Built, Taken };
    struct Entry {
        State state = State::Queued;
        VkPipeline pipeline = VK_NULL_HANDLE;
    };
    void run();

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineCache cache_ = VK_NULL_HANDLE;
    bool nonSolidFill_ = false;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable built_;
    std::deque<Job> jobs_;
    std::deque<std::function<void()>> tasks_;
    std::unordered_map<std::uint64_t, Entry> entries_;
    Progress progress_;
    bool stopping_ = false;
    std::vector<std::thread> workers_;
};

}  // namespace sh::rhi::vkn
