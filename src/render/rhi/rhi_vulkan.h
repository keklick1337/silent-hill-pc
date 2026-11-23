// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Native Vulkan backend of the RHI (RENDER.md sections 2.2a and 2.3): its own instance, device,
// queue, swapchain and memory (volk loads the API at run time, Vulkan Memory Allocator places the memory). It takes
// the same SPIR-V as the SDL3 GPU backend, in SDL3's resource layout, so the shaders are built once for both.
// Standard Vulkan only: core 1.3 (or 1.1 with the dynamic rendering and synchronization2 extensions) and KHR/EXT
// extensions, nothing of one vendor.
#pragma once

#include "render/rhi/rhi.h"

#include <cstdint>
#include <memory>
#include <string>

struct SDL_Window;

namespace sh::rhi {

struct VulkanDeviceDesc {
    // The game window: the swapchain is made on it. Null, or `surfaceless`, renders into images of the backend's own
    // (acquireSwapchainTexture hands one out at the window's size) and presents nothing.
    SDL_Window* window = nullptr;
    // No surface even with a window: SDL's offscreen video driver has none on drivers without VK_EXT_headless_surface,
    // yet the frame and its readback (screenshots) still work.
    bool surfaceless = false;
    bool validation = false;  // VK_LAYER_KHRONOS_validation when the system has it; never required
    bool vsync = true;
    // A CPU device (lavapipe) counts only when asked for: on the CPU the GL backend over llvmpipe is the faster path.
    bool allowCpu = false;
};

// What the device can do, read once at creation; later stages (ray tracing, mesh shaders, HDR) decide on it.
struct VulkanCaps {
    std::uint32_t apiVersion = 0;  // VK_MAKE_API_VERSION of the device as used (at most 1.3)
    std::uint32_t deviceApiVersion = 0;
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    std::uint32_t driverVersion = 0;
    std::string deviceName;
    std::string deviceType;  // "discrete", "integrated", "virtual", "cpu", "other"
    std::string driverName;  // VkPhysicalDeviceDriverProperties, when known
    std::string driverInfo;
    std::uint32_t subgroupSize = 0;
    std::uint64_t deviceLocalBytes = 0;  // sum of the device-local heaps
    std::uint64_t vramBudgetBytes = 0;   // what VK_EXT_memory_budget allows this process at start; 0 = unknown
    bool accelerationStructure = false;  // VK_KHR_acceleration_structure
    bool rayQuery = false;               // VK_KHR_ray_query
    bool rayTracingPipeline = false;     // VK_KHR_ray_tracing_pipeline
    bool rayTracingEnabled = false;      // the device runs with what ray tracing needs (rhi_vulkan_native.h)
    bool meshShader = false;             // VK_EXT_mesh_shader
    bool memoryBudget = false;           // VK_EXT_memory_budget (enabled)
    bool swapchainColorspace = false;    // VK_EXT_swapchain_colorspace (instance)
    bool hdrMetadata = false;            // VK_EXT_hdr_metadata
    bool tessellationShader = false;     // the core feature
    bool timestamps = false;             // the graphics queue writes timestamps
    bool surfaceless = false;            // no swapchain: frames go to the backend's own images
    bool validation = false;             // the validation layer is on
};

// SDL_Init(SDL_INIT_VIDEO) must have run. Fails (to the next backend) when no device has what the backend needs.
Result<std::unique_ptr<Device>> createVulkanDevice(const VulkanDeviceDesc& desc);

// Null for a device of another backend.
const VulkanCaps* vulkanCaps(const Device& device);

// The record as log lines, one feature group per line.
std::string describeVulkanCaps(const VulkanCaps& caps);

// Present mode of the swapchain: vsync = FIFO; otherwise immediate, or mailbox where immediate is missing. No-op for
// a surfaceless device or one of another backend.
void vulkanSetPresentMode(Device& device, bool vsync);

// Frame pacing (RENDER.md section 7.13) with standard Vulkan only. The present mode overrides the vsync choice
// above unless it is 0; a mode the surface lacks falls back to FIFO. One frame in flight makes begin() also wait
// for the frame before; presentWait waits, with VK_KHR_present_wait and VK_KHR_present_id, until the frame before
// the last one is on screen, so at most one frame is queued for the display. No-op without a swapchain.
struct VulkanFramePacing {
    int presentMode = 0;  // 0 auto (vsync), 1 FIFO, 2 mailbox, 3 immediate, 4 FIFO relaxed
    int framesInFlight = 2;
    bool presentWait = false;
};
void vulkanSetFramePacing(Device& device, const VulkanFramePacing& pacing);
// VK_KHR_present_wait and VK_KHR_present_id are enabled on a device with a swapchain.
bool vulkanPresentWaitSupported(const Device& device);

// HDR10 output (RENDER.md section 7.10): the swapchain in VK_COLOR_SPACE_HDR10_ST2084_EXT with a 10-bit format
// when VK_EXT_swapchain_colorspace and the display offer it, plus VK_EXT_hdr_metadata where present. The backend only
// picks the colour space; the engine writes PQ-encoded BT.2020 values into the swapchain image (render/gpu/hdr_pass).
struct VulkanHdrOutput {
    bool enabled = false;
    float paperWhiteNits = 200.f;  // what SDR white (1.0) is shown at
    float peakNits = 1000.f;       // the brightest the tonemapper sends; also the metadata's mastering peak
};
// Takes effect when the swapchain is made next (at once for a change of the luminances).
void vulkanSetHdrOutput(Device& device, const VulkanHdrOutput& hdr);

enum class VulkanHdrBlock : std::uint8_t { None, Off, OtherBackend, NoSurface, NoColorspaceExtension, NoHdr10Format };
struct VulkanHdrStatus {
    bool active = false;                         // the current swapchain is HDR10
    VulkanHdrBlock block = VulkanHdrBlock::Off;  // why not, while it is not
    std::string reason;                          // the same as text for the log
};
VulkanHdrStatus vulkanHdrStatus(const Device& device);

}  // namespace sh::rhi
