// The window's device (RENDER.md section 3): creates the SDL window and either a GL 3.3 core context with the
// RHI GL backend (the backend loads GL itself: no glad), the native Vulkan device (`renderer = vulkan`) or the SDL3
// GPU device (`renderer = sdlgpu`); swap interval,
// fullscreen and window state, context-lost handling, the per-frame command list.
//
// Ownership: SDL_Init(SDL_INIT_VIDEO) is the host's (engine/platform/host_window.cpp), and so are the runtime window
// mode switch (sh::platform::apply_window_mode), the event pump and the pointer confinement; after a mode switch the
// host calls
// reset(). This class owns the window it created, the GL contexts and the RHI device, and destroys them in that
// order's reverse.
#pragma once

#include "render/gpu/backend_name.h"
#include "render/gpu/software_gl.h"
#include "render/rhi/rhi.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

struct SDL_Window;
struct SDL_GLContextState;

namespace sh::gpu {

struct InitParams;

enum class FullscreenMode : std::uint8_t { Windowed = 0, Exclusive = 1, Borderless = 2 };

// Everything window and device creation reads; built from the engine config by device_params().
struct DeviceParams {
    std::string window_name = "Silent Hill";
    int width = 640, height = 480;  // <= 0: the window covers the screen (fullscreen at desktop size)
    FullscreenMode fullscreen = FullscreenMode::Windowed;
    BackendKind backend = BackendKind::Gl;
    // MSAA the scene target is asked for (0, 2, 4, 8): the Targets zone reads it through msaa_samples().
    int msaa_samples = 0;
    // MSAA on the window's own pixel format. Off: the scene renders into an internal target that the present blits
    // into the window, and a blit from a single-sample source into a multisample window is INVALID_OPERATION.
    bool window_msaa = false;
    // renderer = vulkan: a hidden window with a GL context of its own, for the GL code not on the RHI yet (the game
    // window cannot carry both a GL pixel format and a Vulkan swapchain). Not needed once nothing calls GL directly.
    bool legacy_gl_window = true;
    bool raise = true;  // show and raise on creation: input that skips the intro must land on the game
    bool vulkan_validation = false;  // native Vulkan: the Khronos validation layer when the system has it
    // Native Vulkan: the pipeline cache on disk and the prewarm (RENDER.md section 2.2d).
    std::string pipeline_cache_dir;
    std::string game_version;
    bool shader_prewarm = true;
    SoftwareGl software_gl = SoftwareGl::Auto;
};

// The config spelling: `renderer` name, window size, fullscreen 0/1/2, `msaa` (snapped to 0/2/4/8).
DeviceParams device_params(std::string_view renderer, int width, int height, int fullscreen, int msaa);
DeviceParams device_params(const InitParams& params);
int snap_msaa_samples(int samples);

// SDL_CreateWindow flags for the game window of these params.
std::uint64_t window_flags(const DeviceParams& params);

// What the device reported at creation, for the log.
struct HostCaps {
    std::string adapter;      // GL_RENDERER / SDL GPU device name
    std::string vendor;       // GL_VENDOR
    std::string version;      // GL_VERSION / SDL GPU driver
    std::string glsl;         // GL_SHADING_LANGUAGE_VERSION
    float max_anisotropy = 0.f;  // 0 = no anisotropic filtering
    bool software = false;       // the renderer draws on the CPU (is_software_adapter)
};

class DeviceHost {
public:
    static std::unique_ptr<DeviceHost> create(const InitParams& params);
    // nullptr on failure (logged). renderer = vulkan without a usable device falls back to sdlgpu, then GL;
    // sdlgpu falls back to GL.
    static std::unique_ptr<DeviceHost> create(const DeviceParams& params);
    ~DeviceHost();
    DeviceHost(const DeviceHost&) = delete;
    DeviceHost& operator=(const DeviceHost&) = delete;

    SDL_Window* window() const;
    rhi::Device& device();
    bool vulkan() const;  // either Vulkan backend
    BackendKind backend() const;
    const HostCaps& caps() const { return caps_; }
    int msaa_samples() const { return msaa_samples_; }  // what the scene target may use (window MSAA dropped or not)
    const DeviceParams& params() const { return params_; }

    // The open command list of this frame, begun on first use. When the device cannot open one (lost, minimised
    // swapchain on some drivers) a list that drops everything is returned: the frame is skipped, not crashed.
    rhi::CommandList& frame();
    bool frame_open() const { return open_ != nullptr; }
    void submit();  // submit + present + swap; a frame never begun is a no-op
    // Applied only on a change: SDL hands every call to the driver, and NVIDIA's DXGI-layered GL present
    // reconfigures presentation even for the same value.
    void set_swap_interval(int interval);
    int swap_interval() const { return applied_interval_; }
    void reset();  // Forgets the interval so the next frame re-applies it (after a mode switch)

    void window_size(int* w, int* h) const;    // window size in screen points
    void drawable_size(int* w, int* h) const;  // the window's back buffer in pixels

    // A device that stopped working: frames are dropped from then on. The handler runs once (the integration can
    // exit or restart on GL there); none by default, matching native GL which has nothing to fall back to.
    bool lost() const { return lost_; }
    void set_lost_handler(std::function<void(const char* where)> handler) { lost_handler_ = std::move(handler); }

    // renderer = vulkan with legacy_gl_window: the hidden GL window and its context; null otherwise.
    SDL_Window* legacy_gl_window() const { return gl_window_ != window_ ? gl_window_ : nullptr; }
    SDL_GLContextState* gl_context() const { return gl_context_; }

private:
    DeviceHost() = default;
    bool open_window(std::uint64_t flags);
    bool open_gl_window();
    bool init_gl();
    bool init_vulkan();  // the SDL3 GPU device
    bool init_vulkan_native();
    void drop_vulkan_window();
    void probe_gl_caps();
    void note_failure(const char* where, const std::string& error, bool counts_as_loss);

    DeviceParams params_;
    SDL_Window* window_ = nullptr;
    SDL_Window* gl_window_ = nullptr;  // the window the GL context lives on (window_ itself on GL)
    SDL_GLContextState* gl_context_ = nullptr;
    std::unique_ptr<rhi::Device> device_;
    std::unique_ptr<rhi::CommandList> null_list_;
    rhi::CommandList* open_ = nullptr;
    HostCaps caps_;
    int msaa_samples_ = 0;
    int applied_interval_ = -1000;  // nothing applied yet
    int failures_ = 0;              // consecutive frames that failed
    std::uint32_t failure_logs_ = 0;
    bool dropped_ = false;          // this frame's begin failed: no retry until submit()
    bool lost_ = false;
    std::function<void(const char*)> lost_handler_;
};

}  // namespace sh::gpu
