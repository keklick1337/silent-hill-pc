// sh::gpu device host, the window-free part: backend names, config -> creation params, window flags.

#include "framework/sh_test.h"
#include "render/gpu/backend_name.h"
#include "render/gpu/device_host.h"
#include "render/gpu/renderer.h"

#include <SDL3/SDL.h>

namespace g = sh::gpu;

SH_TEST(gpu_backend_names) {
    SH_CHECK(g::backend_from_name("gl") == g::BackendKind::Gl);
    SH_CHECK(g::backend_from_name("OpenGL") == g::BackendKind::Gl);
    SH_CHECK(g::backend_from_name("vulkan") == g::BackendKind::Vulkan);
    SH_CHECK(g::backend_from_name("VK") == g::BackendKind::Vulkan);
    SH_CHECK(!g::backend_from_name("d3d11").has_value());
    SH_CHECK(!g::backend_from_name("").has_value());
    // Names of backends that do not exist, typos and empty values run on GL.
    SH_CHECK(g::resolve_backend("gles") == g::BackendKind::Gl);
    SH_CHECK(g::resolve_backend("angle-vulkan") == g::BackendKind::Gl);
    SH_CHECK(g::resolve_backend("") == g::BackendKind::Gl);
    SH_CHECK(g::resolve_backend("Vulkan") == g::BackendKind::Vulkan);
    SH_CHECK(g::backend_name(g::BackendKind::Gl) == "gl");
    SH_CHECK(g::backend_name(g::BackendKind::Vulkan) == "vulkan");
    SH_CHECK(g::resolve_backend(g::backend_name(g::BackendKind::Vulkan)) == g::BackendKind::Vulkan);
    SH_CHECK(g::backend_from_name("sdlgpu") == g::BackendKind::SdlGpu);
    SH_CHECK(g::backend_name(g::BackendKind::SdlGpu) == "sdlgpu");
    SH_CHECK(g::resolve_backend(g::backend_name(g::BackendKind::SdlGpu)) == g::BackendKind::SdlGpu);
    SH_CHECK(!g::backend_description(g::BackendKind::Gl).empty());
}

SH_TEST(gpu_device_params_from_config) {
    g::DeviceParams p = g::device_params("vulkan", 1280, 720, 2, 5);
    SH_CHECK(p.backend == g::BackendKind::Vulkan);
    SH_CHECK(p.width == 1280 && p.height == 720);
    SH_CHECK(p.fullscreen == g::FullscreenMode::Borderless);
    SH_CHECK(p.msaa_samples == 4);
    SH_CHECK(!p.window_msaa);
    p = g::device_params("typo", 640, 480, 1, 1);
    SH_CHECK(p.backend == g::BackendKind::Gl && p.fullscreen == g::FullscreenMode::Exclusive && p.msaa_samples == 0);
    p = g::device_params("gl", 640, 480, 7, 64);
    SH_CHECK(p.fullscreen == g::FullscreenMode::Windowed && p.msaa_samples == 8);

    SH_CHECK(g::snap_msaa_samples(-3) == 0 && g::snap_msaa_samples(2) == 2 && g::snap_msaa_samples(3) == 2);

    g::InitParams init;
    init.window_name = "Test";
    init.width = 800;
    init.height = 600;
    init.backend = g::BackendKind::Vulkan;
    p = g::device_params(init);
    SH_CHECK(p.window_name == "Test" && p.width == 800 && p.backend == g::BackendKind::Vulkan);
    SH_CHECK(p.fullscreen == g::FullscreenMode::Windowed);
}

SH_TEST(gpu_window_flags) {
    g::DeviceParams p = g::device_params("gl", 640, 480, 0, 0);
    std::uint64_t f = g::window_flags(p);
    SH_CHECK((f & SDL_WINDOW_OPENGL) != 0 && (f & SDL_WINDOW_RESIZABLE) != 0 && (f & SDL_WINDOW_FULLSCREEN) == 0);
    // Vulkan: no GL pixel format on the game window; the native backend makes its surface on it.
    p.backend = g::BackendKind::Vulkan;
    f = g::window_flags(p);
    SH_CHECK((f & SDL_WINDOW_OPENGL) == 0);
    p.backend = g::BackendKind::SdlGpu;
    SH_CHECK((g::window_flags(p) & (SDL_WINDOW_OPENGL | SDL_WINDOW_VULKAN)) == 0);
    p.backend = g::BackendKind::Vulkan;
    p.fullscreen = g::FullscreenMode::Exclusive;
    SH_CHECK((g::window_flags(p) & SDL_WINDOW_FULLSCREEN) != 0);
    // No size: covers the screen.
    p = g::device_params("gl", 0, 0, 0, 0);
    SH_CHECK((g::window_flags(p) & SDL_WINDOW_FULLSCREEN) != 0);
}
