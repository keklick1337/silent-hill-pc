#pragma once

// SDL3 GPU backend of the RHI: Vulkan on Linux and Windows, D3D12 on Windows, Metal on Apple (RENDER.md section 2).
// Shaders are SPIR-V; SDL3 translates them for D3D12 and Metal only with SDL_shadercross, so this backend
// asks SDL for a SPIR-V capable driver (Vulkan) unless told otherwise.

#include "render/rhi/rhi.h"

#include <memory>

struct SDL_Window;

namespace sh::rhi {

struct SdlGpuDeviceDesc {
    const char* driver = nullptr;  // "vulkan", or nullptr = the first SPIR-V driver SDL finds
    bool debug = false;            // validation layers when the system has them
    SDL_Window* window = nullptr;  // optional: claimed for the swapchain
};

// SDL_Init(SDL_INIT_VIDEO) must have run.
Result<std::unique_ptr<Device>> createSdlGpuDevice(const SdlGpuDeviceDesc& desc);

// Present mode of the claimed window: vsync = FIFO; otherwise immediate, or mailbox where immediate is missing.
// No-op for a device without a window or of another backend.
void sdlGpuSetPresentMode(Device& device, bool vsync);

}  // namespace sh::rhi
