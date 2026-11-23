#pragma once

// GL 3.3 core backend of the RHI. It does not create a context: it runs on the one current on the calling
// thread (the game window's SDL GL context).

#include "render/rhi/rhi.h"

#include <cstdint>
#include <functional>
#include <memory>

namespace sh::rhi {

struct GlDeviceDesc {
    // SDL_GL_GetProcAddress or eglGetProcAddress.
    void* (*getProcAddress)(const char* name) = nullptr;
    // Present the default framebuffer (SDL_GL_SwapWindow); empty = offscreen only.
    std::function<void()> swapBuffers;
    // Size of the default framebuffer in pixels, for acquireSwapchainTexture.
    std::function<void(std::uint32_t* width, std::uint32_t* height)> drawableSize;
};

Result<std::unique_ptr<Device>> createGlDevice(const GlDeviceDesc& desc);

}  // namespace sh::rhi
