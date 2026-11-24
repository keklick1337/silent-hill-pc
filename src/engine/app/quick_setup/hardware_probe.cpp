// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/app/quick_setup/hardware_probe.h"

#include "render/gpu/software_gl.h"
#include "render/rhi/rhi_vulkan.h"

#include <SDL3/SDL.h>

#include <cstdio>

namespace sh::quick_setup {

namespace {

constexpr unsigned kGlVendor = 0x1F00;
constexpr unsigned kGlRenderer = 0x1F01;

void probe_gl(Hardware& hw) {
    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_Window* window = SDL_CreateWindow("probe", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (window == nullptr) return;
    if (SDL_GLContext context = SDL_GL_CreateContext(window)) {
        using GetString = const unsigned char* (*)(unsigned);
        const auto get = reinterpret_cast<GetString>(SDL_GL_GetProcAddress("glGetString"));
        if (get != nullptr) {
            const unsigned char* renderer = get(kGlRenderer);
            const unsigned char* vendor = get(kGlVendor);
            hw.gl = true;
            hw.gl_renderer = renderer ? reinterpret_cast<const char*>(renderer) : "";
            if (vendor && hw.gl_renderer.find(reinterpret_cast<const char*>(vendor)) == std::string::npos)
                hw.gl_renderer += std::string(" (") + reinterpret_cast<const char*>(vendor) + ")";
            hw.gl_software = sh::gpu::is_software_adapter(hw.gl_renderer);
#ifdef __APPLE__
            // Apple froze OpenGL at 4.1 and runs it over Metal; MoltenVK is the closer path to the GPU.
            hw.gl_legacy = true;
#endif
        }
        SDL_GL_DestroyContext(context);
    }
    SDL_DestroyWindow(window);
    // The game's own window sets what it needs; nothing of the probe may stay behind.
    SDL_GL_ResetAttributes();
}

void probe_vulkan(Hardware& hw) {
    sh::rhi::VulkanDeviceDesc desc;
    desc.surfaceless = true;
    auto device = sh::rhi::createVulkanDevice(desc);
    if (!device) {
        hw.vulkan_error = device.error();
        return;
    }
    if (const sh::rhi::VulkanCaps* caps = sh::rhi::vulkanCaps(*device.value())) {
        hw.vulkan = true;
        hw.gpu_name = caps->deviceName;
        hw.kind = gpu_kind_from_vulkan_type(caps->deviceType);
        hw.vram_bytes = caps->deviceLocalBytes;
        hw.ray_tracing = caps->rayTracingEnabled;
    }
}

const char* kind_name(GpuKind kind) {
    switch (kind) {
        case GpuKind::Cpu:
            return "cpu";
        case GpuKind::Integrated:
            return "integrated";
        case GpuKind::Discrete:
            return "discrete";
        case GpuKind::Other:
            return "other";
        default:
            return "none";
    }
}

}  // namespace

Hardware probe_hardware() {
    Hardware hw;
    probe_gl(hw);
    probe_vulkan(hw);
    return hw;
}

std::string describe(const Hardware& hw) {
    char text[768];
    std::snprintf(text, sizeof text, "OpenGL %s%s%s; Vulkan %s (%s, %llu MiB, ray tracing %s)%s%s",
                  hw.gl ? hw.gl_renderer.c_str() : "none", hw.gl_software ? " [CPU]" : "",
                  hw.gl_legacy ? " [legacy]" : "", hw.vulkan ? hw.gpu_name.c_str() : "none", kind_name(hw.kind),
                  static_cast<unsigned long long>(hw.vram_bytes >> 20), hw.ray_tracing ? "yes" : "no",
                  hw.vulkan_error.empty() ? "" : ": ", hw.vulkan_error.c_str());
    return text;
}

}  // namespace sh::quick_setup
