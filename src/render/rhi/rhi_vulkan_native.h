// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The native Vulkan backend's own objects, for the engine's passes that the RHI does not express (ray tracing,
// the atmosphere's compute passes and the other native Vulkan features of RENDER.md). Only code linked into
// sh_rhi, which has the Vulkan dependencies, includes this. The handles stay the backend's: nothing here is destroyed
// by its user.
#pragma once

#include "render/rhi/rhi.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace sh::rhi {

inline constexpr std::uint32_t kVulkanFramesInFlight = 2;

struct VulkanNative {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t queueFamily = 0;
    VmaAllocator allocator = nullptr;
    std::uint32_t apiVersion = 0;
    // The device runs with the KHR acceleration structure and ray query features, buffer device addresses and
    // non-uniform sampler indexing; every vertex and index buffer can then be read by an acceleration structure build.
    bool rayTracing = false;
};

// Null for a device of another backend.
const VulkanNative* vulkanNative(Device& device);

// The recording frame's command buffer, with any open render pass or copy section ended first (the next RHI pass
// begins as usual after it). Null when no frame is recording.
VkCommandBuffer vulkanNativeCommands(Device& device);

// Which of the frames in flight is recording (0 .. kVulkanFramesInFlight - 1). When a frame begins, the previous
// submission of its slot has completed, so per-slot objects of the caller are free to reuse.
std::uint32_t vulkanFrameSlot(Device& device);

// Calls `release` once the GPU finished every submission that may use what it frees.
void vulkanRetire(Device& device, std::function<void()> release);

struct VulkanImageInfo {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;  // the sampled view
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;  // where it rests between passes
    std::uint32_t width = 0, height = 0;
};
bool vulkanImageInfo(Device& device, TextureH texture, VulkanImageInfo* out);
VkBuffer vulkanBuffer(Device& device, BufferH buffer);

// Registers an image of the caller's as a sampled RHI texture resting in SHADER_READ_ONLY_OPTIMAL, so the RHI's
// shaders can sample it. The caller leaves it in that layout after its own work; destroy() only forgets it.
TextureH vulkanAdoptImage(Device& device, VkImage image, VkImageView view, VkFormat format, std::uint32_t width,
                          std::uint32_t height);

// A timestamp span of the profiler (RenderPassDesc::name's list), around native work.
void vulkanBeginSpan(Device& device, const char* name);
void vulkanEndSpan(Device& device);

// Ray tracing at device creation: the extensions and feature structures the device must enable. `supported` is
// false (and nothing is added) unless every one is there.
struct VulkanRayTracingSetup {
    bool supported = false;
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR as{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceRayQueryFeaturesKHR rq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
};
// Decides `setup` for a device; on success appends the extension names and links the structures in front of
// `*chain` (the pNext of VkPhysicalDeviceFeatures2). `setup` must outlive vkCreateDevice.
void vulkanRayTracingChain(VkPhysicalDevice physical, std::uint32_t apiVersion,
                           const std::vector<VkExtensionProperties>& extensions, VulkanRayTracingSetup* setup,
                           std::vector<const char*>* names, void** chain);

}  // namespace sh::rhi

namespace sh::rhi::vkn {

class Rendering;

struct Context {
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = nullptr;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    std::uint32_t queueFamily = 0;
    VkQueueFlags queueFlags = 0;  // of the one queue everything is submitted to
    // Passes, barriers and pipelines' attachment formats: dynamic rendering and synchronization2, or the Vulkan 1.0
    // objects and calls where the device lacks them.
    Rendering* rendering = nullptr;
};

struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;         // every aspect (the attachment view)
    VkImageView sampledView = VK_NULL_HANDLE;  // depth only for a sampled depth texture
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspect = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout rest = VK_IMAGE_LAYOUT_UNDEFINED;  // where the backend expects it between uses
};

// Null for a device of another backend.
const Context* context(Device& device);
// The frame's command buffer outside any render pass or copy section; null when no frame is being recorded.
VkCommandBuffer record(Device& device);
// Frames in flight and the slot of the one being recorded: per-slot objects are free again when the slot comes round.
std::uint32_t frameSlots();
std::uint32_t frameSlot(Device& device);
// The number of the submission being recorded (rises by one per frame).
std::uint64_t frameSerial(Device& device);
bool image(Device& device, TextureH texture, Image* out);
// A tracked layout change of a backend texture (recorded into the frame).
void transition(Device& device, TextureH texture, VkImageLayout layout);
// A GPU time span for the profiler (RenderPassDesc::name's list); nothing while another span is open.
void beginSpan(Device& device, const char* name);
void endSpan(Device& device);
// Runs `release` once the GPU finished everything recorded so far.
void retire(Device& device, std::function<void()> release);

}  // namespace sh::rhi::vkn
