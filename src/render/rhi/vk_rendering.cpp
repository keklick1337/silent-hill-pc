// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#include "render/rhi/vk_rendering.h"

#include <algorithm>

namespace sh::rhi::vkn {

namespace {

// The synchronization2 bits below 32 are the Vulkan 1.0 bits; the ones above are finer kinds of those.
VkPipelineStageFlags stages1(VkPipelineStageFlags2 s, bool source) {
    VkPipelineStageFlags out = static_cast<VkPipelineStageFlags>(s & 0xFFFFFFFFull);
    if (s & (VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT |
             VK_PIPELINE_STAGE_2_CLEAR_BIT))
        out |= VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (s & (VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT | VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT))
        out |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    if (s & VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT) out |= VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    constexpr VkPipelineStageFlags2 known =
        0xFFFFFFFFull | VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT |
        VK_PIPELINE_STAGE_2_CLEAR_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT |
        VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT;
    if (s & ~known) out |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    // NONE has no 1.0 spelling: the first or the last stage waits for nothing.
    if (out == 0) out = source ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    return out;
}

VkAccessFlags access1(VkAccessFlags2 a) {
    VkAccessFlags out = static_cast<VkAccessFlags>(a & 0xFFFFFFFFull);
    if (a & (VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT)) out |= VK_ACCESS_SHADER_READ_BIT;
    if (a & VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT) out |= VK_ACCESS_SHADER_WRITE_BIT;
    constexpr VkAccessFlags2 known = 0xFFFFFFFFull | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT |
                                     VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    if (a & ~known) out |= VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    return out;
}

}  // namespace

void Rendering::init(VkDevice device, bool core13, bool dynamicRendering, bool synchronization2,
                     std::uint32_t frameSlots) {
    device_ = device;
    dynamic_ = dynamicRendering;
    sync2_ = synchronization2;
    if (dynamic_) {
        beginRendering_ = core13 ? vkCmdBeginRendering : vkCmdBeginRenderingKHR;
        endRendering_ = core13 ? vkCmdEndRendering : vkCmdEndRenderingKHR;
        dynamic_ = beginRendering_ && endRendering_;
    }
    if (sync2_) {
        pipelineBarrier2_ = core13 ? vkCmdPipelineBarrier2 : vkCmdPipelineBarrier2KHR;
        queueSubmit2_ = core13 ? vkQueueSubmit2 : vkQueueSubmit2KHR;
        writeTimestamp2_ = core13 ? vkCmdWriteTimestamp2 : vkCmdWriteTimestamp2KHR;
        sync2_ = pipelineBarrier2_ && queueSubmit2_ && writeTimestamp2_;
    }
    framebuffers_.assign(std::max<std::uint32_t>(frameSlots, 1), {});
    slot_ = 0;
}

void Rendering::destroy() {
    if (!device_) return;
    for (auto& slot : framebuffers_)
        for (const Framebuffer& f : slot) vkDestroyFramebuffer(device_, f.framebuffer, nullptr);
    framebuffers_.clear();
    for (auto& [key, p] : passes_) vkDestroyRenderPass(device_, p, nullptr);
    passes_.clear();
    device_ = VK_NULL_HANDLE;
}

void Rendering::frameStart(std::uint32_t slot) {
    if (framebuffers_.empty()) return;
    slot_ = slot % framebuffers_.size();
    for (const Framebuffer& f : framebuffers_[slot_]) vkDestroyFramebuffer(device_, f.framebuffer, nullptr);
    framebuffers_[slot_].clear();
}

void Rendering::forget(VkImageView view) {
    if (framebuffers_.empty() || !view) return;
    // Kept until the slot comes round (a recorded command may use it); only the lookup loses it.
    for (Framebuffer& f : framebuffers_[slot_])
        if (std::find(f.views.begin(), f.views.end(), view) != f.views.end()) f.pass = VK_NULL_HANDLE;
}

VkRenderPass Rendering::pass(const PassKey& key) {
    if (auto it = passes_.find(key); it != passes_.end()) return it->second;
    VkAttachmentDescription att[kMaxColors + 1]{};
    VkAttachmentReference colors[kMaxColors]{};
    VkAttachmentReference depth{};
    std::uint32_t n = 0;
    auto describe = [&](const Attachment& a, VkAttachmentReference& ref) {
        if (a.format == VK_FORMAT_UNDEFINED) {
            ref = {VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED};
            return;
        }
        VkAttachmentDescription& d = att[n];
        d.format = a.format;
        d.samples = VK_SAMPLE_COUNT_1_BIT;
        d.loadOp = a.load;
        d.storeOp = a.store;
        d.stencilLoadOp = a.stencilLoad;
        d.stencilStoreOp = a.stencilStore;
        // As with dynamic rendering, the image is in its attachment layout before the pass and stays in it.
        d.initialLayout = a.layout;
        d.finalLayout = a.layout;
        ref = {n++, a.layout};
    };
    for (std::uint32_t i = 0; i < key.colors; ++i) describe(key.color[i], colors[i]);
    const bool hasDepth = key.depth.format != VK_FORMAT_UNDEFINED;
    if (hasDepth) describe(key.depth, depth);
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = key.colors;
    sub.pColorAttachments = colors;
    sub.pDepthStencilAttachment = hasDepth ? &depth : nullptr;
    // No subpass dependencies: the backend's barriers before and after the pass order it, as with dynamic rendering.
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = n;
    info.pAttachments = att;
    info.subpassCount = 1;
    info.pSubpasses = &sub;
    VkRenderPass p = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device_, &info, nullptr, &p) != VK_SUCCESS) return VK_NULL_HANDLE;
    passes_.emplace(key, p);
    return p;
}

void Rendering::pipeline(VkGraphicsPipelineCreateInfo& info, VkPipelineRenderingCreateInfo& rendering) {
    if (dynamic_) {
        rendering.pNext = info.pNext;
        info.pNext = &rendering;
        return;
    }
    PassKey key;
    key.colors = std::min(rendering.colorAttachmentCount, kMaxColors);
    for (std::uint32_t i = 0; i < key.colors; ++i) {
        key.color[i].format = rendering.pColorAttachmentFormats[i];
        key.color[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    key.depth.format = rendering.depthAttachmentFormat != VK_FORMAT_UNDEFINED ? rendering.depthAttachmentFormat
                                                                              : rendering.stencilAttachmentFormat;
    key.depth.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    info.renderPass = pass(key);
    info.subpass = 0;
}

void Rendering::begin(VkCommandBuffer cmd, const VkRenderingInfo& ri, const VkFormat* colorFormats,
                      VkFormat depthFormat) {
    if (dynamic_) {
        beginRendering_(cmd, &ri);
        return;
    }
    PassKey key;
    key.colors = std::min(ri.colorAttachmentCount, kMaxColors);
    std::vector<VkImageView> views;
    VkClearValue clears[kMaxColors + 1]{};
    std::uint32_t n = 0;
    for (std::uint32_t i = 0; i < key.colors; ++i) {
        const VkRenderingAttachmentInfo& c = ri.pColorAttachments[i];
        if (!c.imageView) continue;  // an unused slot, as a pipeline with VK_FORMAT_UNDEFINED there has
        Attachment& a = key.color[i];
        a.format = colorFormats[i];
        a.load = c.loadOp;
        a.store = c.storeOp;
        a.layout = c.imageLayout;
        views.push_back(c.imageView);
        clears[n++] = c.clearValue;
    }
    const VkRenderingAttachmentInfo* d = ri.pDepthAttachment && ri.pDepthAttachment->imageView ? ri.pDepthAttachment
                                                                                                 : nullptr;
    const VkRenderingAttachmentInfo* s =
        ri.pStencilAttachment && ri.pStencilAttachment->imageView ? ri.pStencilAttachment : nullptr;
    if (d || s) {
        Attachment& a = key.depth;
        a.format = depthFormat;
        a.layout = d ? d->imageLayout : s->imageLayout;
        if (d) {
            a.load = d->loadOp;
            a.store = d->storeOp;
        }
        if (s) {
            a.stencilLoad = s->loadOp;
            a.stencilStore = s->storeOp;
        }
        views.push_back(d ? d->imageView : s->imageView);
        clears[n].depthStencil.depth = d ? d->clearValue.depthStencil.depth : 1.f;
        clears[n].depthStencil.stencil = s ? s->clearValue.depthStencil.stencil : 0;
        ++n;
    }
    const VkRenderPass rp = pass(key);
    const std::uint32_t w = static_cast<std::uint32_t>(ri.renderArea.offset.x) + ri.renderArea.extent.width;
    const std::uint32_t h = static_cast<std::uint32_t>(ri.renderArea.offset.y) + ri.renderArea.extent.height;
    const std::uint32_t layers = std::max<std::uint32_t>(ri.layerCount, 1);
    VkFramebuffer fb = VK_NULL_HANDLE;
    auto& list = framebuffers_[slot_];
    for (const Framebuffer& f : list) {
        if (f.pass == rp && f.width == w && f.height == h && f.layers == layers && f.views == views) {
            fb = f.framebuffer;
            break;
        }
    }
    if (!fb && rp) {
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass = rp;
        fi.attachmentCount = static_cast<std::uint32_t>(views.size());
        fi.pAttachments = views.data();
        fi.width = w;
        fi.height = h;
        fi.layers = layers;
        if (vkCreateFramebuffer(device_, &fi, nullptr, &fb) != VK_SUCCESS) fb = VK_NULL_HANDLE;
        if (fb) list.push_back({rp, std::move(views), w, h, layers, fb});
    }
    // Without a pass the draws that follow would be invalid; with the device out of memory nothing can be drawn.
    if (!fb) return;
    VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    bi.renderPass = rp;
    bi.framebuffer = fb;
    bi.renderArea = ri.renderArea;
    bi.clearValueCount = n;
    bi.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
}

void Rendering::end(VkCommandBuffer cmd) const {
    if (dynamic_)
        endRendering_(cmd);
    else
        vkCmdEndRenderPass(cmd);
}

void Rendering::barrier(VkCommandBuffer cmd, const VkDependencyInfo& dep) const {
    if (sync2_) {
        pipelineBarrier2_(cmd, &dep);
        return;
    }
    VkPipelineStageFlags2 src = 0, dst = 0;
    std::vector<VkMemoryBarrier> memory(dep.memoryBarrierCount);
    for (std::uint32_t i = 0; i < dep.memoryBarrierCount; ++i) {
        const VkMemoryBarrier2& b = dep.pMemoryBarriers[i];
        src |= b.srcStageMask;
        dst |= b.dstStageMask;
        memory[i] = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, access1(b.srcAccessMask), access1(b.dstAccessMask)};
    }
    std::vector<VkBufferMemoryBarrier> buffers(dep.bufferMemoryBarrierCount);
    for (std::uint32_t i = 0; i < dep.bufferMemoryBarrierCount; ++i) {
        const VkBufferMemoryBarrier2& b = dep.pBufferMemoryBarriers[i];
        src |= b.srcStageMask;
        dst |= b.dstStageMask;
        buffers[i] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                      nullptr,
                      access1(b.srcAccessMask),
                      access1(b.dstAccessMask),
                      b.srcQueueFamilyIndex,
                      b.dstQueueFamilyIndex,
                      b.buffer,
                      b.offset,
                      b.size};
    }
    std::vector<VkImageMemoryBarrier> images(dep.imageMemoryBarrierCount);
    for (std::uint32_t i = 0; i < dep.imageMemoryBarrierCount; ++i) {
        const VkImageMemoryBarrier2& b = dep.pImageMemoryBarriers[i];
        src |= b.srcStageMask;
        dst |= b.dstStageMask;
        images[i] = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                     nullptr,
                     access1(b.srcAccessMask),
                     access1(b.dstAccessMask),
                     b.oldLayout,
                     b.newLayout,
                     b.srcQueueFamilyIndex,
                     b.dstQueueFamilyIndex,
                     b.image,
                     b.subresourceRange};
    }
    vkCmdPipelineBarrier(cmd, stages1(src, true), stages1(dst, false), dep.dependencyFlags,
                         static_cast<std::uint32_t>(memory.size()), memory.data(),
                         static_cast<std::uint32_t>(buffers.size()), buffers.data(),
                         static_cast<std::uint32_t>(images.size()), images.data());
}

VkResult Rendering::submit(VkQueue queue, std::uint32_t count, const VkSubmitInfo2* submits, VkFence fence) const {
    if (sync2_) return queueSubmit2_(queue, count, submits, fence);
    struct Lists {
        std::vector<VkSemaphore> wait;
        std::vector<VkPipelineStageFlags> stages;
        std::vector<VkCommandBuffer> cmds;
        std::vector<VkSemaphore> signal;
    };
    std::vector<Lists> lists(count);
    std::vector<VkSubmitInfo> infos(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const VkSubmitInfo2& s = submits[i];
        Lists& l = lists[i];
        for (std::uint32_t k = 0; k < s.waitSemaphoreInfoCount; ++k) {
            l.wait.push_back(s.pWaitSemaphoreInfos[k].semaphore);
            l.stages.push_back(stages1(s.pWaitSemaphoreInfos[k].stageMask, false));
        }
        for (std::uint32_t k = 0; k < s.commandBufferInfoCount; ++k)
            l.cmds.push_back(s.pCommandBufferInfos[k].commandBuffer);
        for (std::uint32_t k = 0; k < s.signalSemaphoreInfoCount; ++k)
            l.signal.push_back(s.pSignalSemaphoreInfos[k].semaphore);
        VkSubmitInfo& o = infos[i];
        o = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
        o.waitSemaphoreCount = static_cast<std::uint32_t>(l.wait.size());
        o.pWaitSemaphores = l.wait.data();
        o.pWaitDstStageMask = l.stages.data();
        o.commandBufferCount = static_cast<std::uint32_t>(l.cmds.size());
        o.pCommandBuffers = l.cmds.data();
        o.signalSemaphoreCount = static_cast<std::uint32_t>(l.signal.size());
        o.pSignalSemaphores = l.signal.data();
    }
    return vkQueueSubmit(queue, count, infos.data(), fence);
}

void Rendering::timestamp(VkCommandBuffer cmd, VkPipelineStageFlags2 stage, VkQueryPool pool,
                          std::uint32_t query) const {
    if (sync2_) {
        writeTimestamp2_(cmd, stage, pool, query);
        return;
    }
    // One stage bit: the latest one the mask names.
    const VkPipelineStageFlags s = stages1(stage, false);
    VkPipelineStageFlagBits bit = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    if (!(s & (VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT)))
        for (int b = 31; b >= 0; --b)
            if (s & (1u << b)) {
                bit = static_cast<VkPipelineStageFlagBits>(1u << b);
                break;
            }
    vkCmdWriteTimestamp(cmd, bit, pool, query);
}

}  // namespace sh::rhi::vkn
