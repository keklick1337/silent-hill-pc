// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Render passes and synchronisation on any Vulkan 1.1 device (docs/RENDER.md 2.2a). The backend records dynamic
// rendering and synchronization2 structures in one code path; on a device without them (many phones on Vulkan 1.1)
// a pass becomes a VkRenderPass with a VkFramebuffer and a barrier or a submission the Vulkan 1.0 call with the same
// meaning. Only code built against the vendored Vulkan headers includes this.
#pragma once

#include <volk.h>

#include <cstdint>
#include <map>
#include <vector>

namespace sh::rhi::vkn {

class Rendering {
public:
    // `dynamicRendering` / `synchronization2`: the device was made with them (core 1.3 or the KHR extensions).
    void init(VkDevice device, bool core13, bool dynamicRendering, bool synchronization2, std::uint32_t frameSlots);
    void destroy();

    bool dynamicRendering() const { return dynamic_; }
    bool synchronization2() const { return sync2_; }

    // `colorFormats` has info.colorAttachmentCount entries, `depthFormat` is the format of the depth (and stencil)
    // attachment: a render pass names its formats, VkRenderingInfo does not.
    void begin(VkCommandBuffer cmd, const VkRenderingInfo& info, const VkFormat* colorFormats, VkFormat depthFormat);
    void end(VkCommandBuffer cmd) const;
    void barrier(VkCommandBuffer cmd, const VkDependencyInfo& dep) const;
    VkResult submit(VkQueue queue, std::uint32_t count, const VkSubmitInfo2* submits, VkFence fence) const;
    void timestamp(VkCommandBuffer cmd, VkPipelineStageFlags2 stage, VkQueryPool pool, std::uint32_t query) const;
    // Chains `rendering` into `info` with dynamic rendering, else gives it a render pass compatible with those
    // formats (load and store ops and layouts do not matter for compatibility).
    void pipeline(VkGraphicsPipelineCreateInfo& info, VkPipelineRenderingCreateInfo& rendering);

    // The slot's fence was waited for: the framebuffers made while it was last recorded are free.
    void frameStart(std::uint32_t slot);
    // The view goes away: the open frame must not find a framebuffer for a new view that reuses its handle.
    void forget(VkImageView view);

private:
    static constexpr std::uint32_t kMaxColors = 4;
    struct Attachment {
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkAttachmentLoadOp load = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        VkAttachmentStoreOp store = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        VkAttachmentLoadOp stencilLoad = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        VkAttachmentStoreOp stencilStore = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        auto operator<=>(const Attachment&) const = default;
    };
    struct PassKey {
        std::uint32_t colors = 0;
        Attachment color[kMaxColors]{};
        Attachment depth{};
        auto operator<=>(const PassKey&) const = default;
    };
    struct Framebuffer {
        VkRenderPass pass = VK_NULL_HANDLE;
        std::vector<VkImageView> views;
        std::uint32_t width = 0, height = 0, layers = 0;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };

    VkRenderPass pass(const PassKey& key);

    VkDevice device_ = VK_NULL_HANDLE;
    bool dynamic_ = false;
    bool sync2_ = false;
    PFN_vkCmdBeginRendering beginRendering_ = nullptr;
    PFN_vkCmdEndRendering endRendering_ = nullptr;
    PFN_vkCmdPipelineBarrier2 pipelineBarrier2_ = nullptr;
    PFN_vkQueueSubmit2 queueSubmit2_ = nullptr;
    PFN_vkCmdWriteTimestamp2 writeTimestamp2_ = nullptr;
    std::map<PassKey, VkRenderPass> passes_;
    // Per frame slot: made while the slot was recorded, destroyed when it comes round. The open frame looks its
    // framebuffers up in its own list only, so a view freed in between can never be found.
    std::vector<std::vector<Framebuffer>> framebuffers_;
    std::uint32_t slot_ = 0;
};

}  // namespace sh::rhi::vkn
