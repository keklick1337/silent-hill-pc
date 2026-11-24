#include "render/gpu/device_host.h"

#include "engine/desktop/video_driver.h"
#include "engine/log/sh_log.h"
#include "render/gpu/renderer.h"
#include "render/rhi/rhi_gl.h"
#include "render/rhi/rhi_sdlgpu.h"
#include "render/rhi/rhi_vulkan.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace sh::gpu {

namespace {

// Consecutive failed frames before the device counts as lost: a minimised or resizing swapchain fails a frame or
// two on some drivers and must not end the game.
constexpr int kLostAfterFrames = 120;
constexpr std::uint32_t kFailureLogEvery = 600;

// GL entry points the probe needs; the RHI GL backend loads its own set.
using GlGetString = const unsigned char* (*)(unsigned name);
using GlGetStringi = const unsigned char* (*)(unsigned name, unsigned index);
using GlGetIntegerv = void (*)(unsigned name, int* data);
using GlGetFloatv = void (*)(unsigned name, float* data);
constexpr unsigned kGlVendor = 0x1F00, kGlRenderer = 0x1F01, kGlVersion = 0x1F02, kGlExtensions = 0x1F03;
constexpr unsigned kGlShadingLanguageVersion = 0x8B8C, kGlNumExtensions = 0x821D;
constexpr unsigned kGlMaxTextureMaxAnisotropy = 0x84FF;

void* gl_proc(const char* name) { return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name)); }

template <class Fn>
Fn gl_fn(const char* name) {
    return reinterpret_cast<Fn>(SDL_GL_GetProcAddress(name));
}

std::string gl_string(GlGetString get, unsigned name) {
    const unsigned char* s = get ? get(name) : nullptr;
    return s ? std::string(reinterpret_cast<const char*>(s)) : std::string();
}

void set_gl_context_attributes(bool msaa, int samples) {
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, msaa ? 1 : 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, msaa ? samples : 0);
}

// Frames the device could not open still run their draw code; it lands here and goes nowhere.
class NullPass final : public rhi::RenderPass {
public:
    void bindPipeline(rhi::PipelineH) override {}
    void setViewport(const rhi::Viewport&) override {}
    void setScissor(const rhi::Rect&) override {}
    void setBlendConstants(const std::array<float, 4>&) override {}
    void setStencilReference(std::uint8_t) override {}
    void bindVertexBuffer(rhi::BufferH, std::uint32_t) override {}
    void bindIndexBuffer(rhi::BufferH, rhi::IndexType, std::uint32_t) override {}
    void bindSampler(rhi::ShaderStage, std::uint32_t, rhi::TextureH, rhi::SamplerH) override {}
    void pushUniforms(rhi::ShaderStage, std::uint32_t, std::span<const std::byte>) override {}
    void draw(std::uint32_t, std::uint32_t) override {}
    void drawIndexed(std::uint32_t, std::uint32_t, std::int32_t) override {}
};

class NullList final : public rhi::CommandList {
public:
    void uploadTexture(rhi::TextureH, const rhi::Rect&, std::span<const std::byte>) override {}
    void uploadBuffer(rhi::BufferH, std::uint32_t, std::span<const std::byte>) override {}
    void copyTexture(rhi::TextureH, const rhi::Rect&, rhi::TextureH, std::int32_t, std::int32_t) override {}
    void blitTexture(rhi::TextureH, const rhi::Rect&, rhi::TextureH, const rhi::Rect&, rhi::Filter) override {}
    void resolveTexture(rhi::TextureH, rhi::TextureH) override {}
    rhi::RenderPass* beginRenderPass(const rhi::RenderPassDesc&) override { return &pass_; }
    void endRenderPass() override {}
    rhi::TextureH acquireSwapchainTexture(std::uint32_t* width, std::uint32_t* height) override {
        if (width) *width = 0;
        if (height) *height = 0;
        return {};
    }

private:
    NullPass pass_;
};

}  // namespace

int snap_msaa_samples(int samples) {
    if (samples >= 8) return 8;
    if (samples >= 4) return 4;
    if (samples >= 2) return 2;
    return 0;
}

DeviceParams device_params(std::string_view renderer, int width, int height, int fullscreen, int msaa) {
    DeviceParams p;
    p.width = width;
    p.height = height;
    p.fullscreen = fullscreen == 1   ? FullscreenMode::Exclusive
                   : fullscreen == 2 ? FullscreenMode::Borderless
                                     : FullscreenMode::Windowed;
    p.backend = resolve_backend(renderer);
    p.msaa_samples = snap_msaa_samples(msaa);
    return p;
}

DeviceParams device_params(const InitParams& params) {
    DeviceParams p = device_params(backend_name(params.backend), params.width, params.height, params.fullscreen,
                                   params.msaa);
    p.vulkan_validation = params.vulkan_validation;
    p.software_gl = static_cast<SoftwareGl>(params.software_gl);
    if (params.window_name) p.window_name = params.window_name;
    // Nothing draws with GL outside the RHI: no hidden GL window beside a Vulkan swapchain.
    p.legacy_gl_window = false;
    return p;
}

std::uint64_t window_flags(const DeviceParams& params) {
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
    // Vulkan: the game window gets no GL pixel format, the SDL3 GPU device claims it.
    if (params.backend == BackendKind::Gl) flags |= SDL_WINDOW_OPENGL;
    // SDL3: FULLSCREEN alone is borderless desktop fullscreen; exclusive is a display mode set on top of it.
    if (params.fullscreen != FullscreenMode::Windowed || params.width <= 0 || params.height <= 0)
        flags |= SDL_WINDOW_FULLSCREEN;
    return flags | sh::desktop::hidden_window_flag();
}

std::unique_ptr<DeviceHost> DeviceHost::create(const InitParams& params) { return create(device_params(params)); }

std::unique_ptr<DeviceHost> DeviceHost::create(const DeviceParams& params) {
    std::unique_ptr<DeviceHost> host(new DeviceHost());
    host->params_ = params;
    host->msaa_samples_ = snap_msaa_samples(params.msaa_samples);
    host->null_list_ = std::make_unique<NullList>();

    // Debugging a fullscreen game needs the debugger's window above it.
    SDL_SetHint(SDL_HINT_WINDOW_ALLOW_TOPMOST, "0");

    if (host->params_.backend == BackendKind::Vulkan) {
        if (host->init_vulkan_native()) return host;
        host->params_.backend = BackendKind::SdlGpu;
    }
    if (host->params_.backend == BackendKind::SdlGpu) {
        if (host->init_vulkan()) return host;
        host->params_.backend = BackendKind::Gl;  // a config asking for Vulkan must still boot the game
    }
    if (!host->init_gl()) return nullptr;
    return host;
}

DeviceHost::~DeviceHost() {
    if (device_) {
        if (open_) (void)device_->submit();
        device_->waitIdle();
        device_.reset();  // its GL objects go while the context is still current
    }
    if (gl_context_) SDL_GL_DestroyContext(gl_context_);
    if (gl_window_ && gl_window_ != window_) SDL_DestroyWindow(gl_window_);
    if (window_) SDL_DestroyWindow(window_);
}

bool DeviceHost::open_window(std::uint64_t flags) {
    const DeviceParams& p = params_;
    window_ = SDL_CreateWindow(p.window_name.c_str(), p.width, p.height, static_cast<SDL_WindowFlags>(flags));
    if (!window_) return false;
    if (p.fullscreen == FullscreenMode::Exclusive && p.width > 0 && p.height > 0) {
        SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
        if (display == 0) display = SDL_GetPrimaryDisplay();
        SDL_DisplayMode mode;
        if (SDL_GetClosestFullscreenDisplayMode(display, p.width, p.height, 0.0f, false, &mode))
            SDL_SetWindowFullscreenMode(window_, &mode);
    }
    if (p.raise && sh::desktop::hidden_window_flag() == 0) {
        // The program that spawned the game (a shell, a file manager, the previous process on restart) may keep
        // keyboard focus: the first key press would land there instead of skipping the intro.
        SDL_ShowWindow(window_);
        SDL_RaiseWindow(window_);
    }
    return true;
}

bool DeviceHost::open_gl_window() {
    const bool window_msaa = params_.window_msaa && msaa_samples_ > 0;
    set_gl_context_attributes(window_msaa, msaa_samples_);
    const std::uint64_t flags = window_flags(params_);
    if (!open_window(flags) && window_msaa) {
        // No multisample pixel format: the game still launches, without antialiasing.
        SH_WARN("[GPU] window creation with %dx MSAA failed (%s); retrying without MSAA", msaa_samples_, SDL_GetError());
        msaa_samples_ = 0;
        set_gl_context_attributes(false, 0);
        open_window(flags);
    }
    if (!window_) {
        SH_ERR("[GPU] failed to create the SDL window: %s", SDL_GetError());
        return false;
    }
    gl_context_ = SDL_GL_CreateContext(window_);
    if (!gl_context_) {
        SH_WARN("[GPU] OpenGL 3.3 core is not supported (%s)", SDL_GetError());
        SDL_DestroyWindow(window_);  // unloads the GL library, so another one can be loaded
        window_ = nullptr;
        return false;
    }
    gl_window_ = window_;
    return true;
}

bool DeviceHost::init_gl() {
    if (params_.software_gl == SoftwareGl::Force) {
        request_system_software_gl();
        use_bundled_software_gl();
    }
    // The bundled llvmpipe replaces only a driver without GL 3.3, never a working one.
    if (!open_gl_window() &&
        !(params_.software_gl == SoftwareGl::Auto && use_bundled_software_gl() && open_gl_window())) {
        SH_ERR("[GPU] no OpenGL 3.3 core context. Please update the video drivers.");
        return false;
    }

    rhi::GlDeviceDesc desc;
    desc.getProcAddress = gl_proc;
    SDL_Window* const window = window_;
    // The present pass draws or blits into the swapchain (framebuffer 0), so the window framebuffer is the one
    // bound at the swap: GL-on-Metal (macOS 26) presents black when an FBO is still bound.
    desc.swapBuffers = [window] { SDL_GL_SwapWindow(window); };
    desc.drawableSize = [window](std::uint32_t* w, std::uint32_t* h) {
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(window, &pw, &ph);
        *w = static_cast<std::uint32_t>(pw > 0 ? pw : 0);
        *h = static_cast<std::uint32_t>(ph > 0 ? ph : 0);
    };
    auto device = rhi::createGlDevice(desc);
    if (!device) {
        SH_ERR("[GPU] the OpenGL device could not be created: %s", device.error().c_str());
        return false;
    }
    device_ = std::move(device.value());
    probe_gl_caps();
    SH_LOG("[GPU] renderer: %s", std::string(backend_description(BackendKind::Gl)).c_str());
    return true;
}

void DeviceHost::drop_vulkan_window() {
    device_.reset();
    if (window_) SDL_DestroyWindow(window_);  // its flags may not suit the next backend: that one makes its own
    window_ = nullptr;
}

bool DeviceHost::init_vulkan_native() {
    // SDL's offscreen driver has a Vulkan surface only with VK_EXT_headless_surface, which NVIDIA lacks: there the
    // backend renders into images of its own, and the screenshot reads them back as on a real window.
    const char* video = SDL_GetCurrentVideoDriver();
    const bool surfaceless = video && std::strcmp(video, "offscreen") == 0;
    if (!rhi::loadVulkanLibrary()) {
        SH_WARN("[GPU] renderer = vulkan: no Vulkan library (MoltenVK); trying the SDL3 GPU device");
        return false;
    }
    std::uint64_t flags = window_flags(params_);
    if (!surfaceless) flags |= SDL_WINDOW_VULKAN;
    if (!open_window(flags)) {
        SH_WARN("[GPU] renderer = vulkan: window creation failed (%s); trying the SDL3 GPU device", SDL_GetError());
        drop_vulkan_window();
        return false;
    }
    rhi::VulkanDeviceDesc desc;
    desc.window = window_;
    desc.surfaceless = surfaceless;
    desc.validation = params_.vulkan_validation;
    auto device = rhi::createVulkanDevice(desc);
    if (!device) {
        SH_WARN("[GPU] renderer = vulkan is not available (%s); trying the SDL3 GPU device", device.error().c_str());
        drop_vulkan_window();
        return false;
    }
    device_ = std::move(device.value());
    const rhi::Caps& c = device_->caps();
    caps_.adapter = c.adapter;
    caps_.version = c.driver;
    if (const rhi::VulkanCaps* vk = rhi::vulkanCaps(*device_)) {
        // The record later stages decide on (RENDER.md section 2.2a), one log line per group.
        const std::string text = rhi::describeVulkanCaps(*vk);
        std::size_t from = 0;
        while (from < text.size()) {
            const std::size_t to = std::min(text.find('\n', from), text.size());
            SH_LOG("[GPU] %s", text.substr(from, to - from).c_str());
            from = to + 1;
        }
        char vendor[16];
        std::snprintf(vendor, sizeof vendor, "0x%04x", vk->vendorId);
        caps_.vendor = vendor;
    }
    SH_LOG("[GPU] renderer: %s (%s)", std::string(backend_description(BackendKind::Vulkan)).c_str(), c.adapter.c_str());
    return true;
}

bool DeviceHost::init_vulkan() {
    if (!open_window(window_flags(params_))) {
        SH_WARN("[GPU] renderer = sdlgpu: window creation failed (%s); falling back to OpenGL", SDL_GetError());
        return false;
    }
    auto fail = [this](const std::string& why) {
        SH_WARN("[GPU] renderer = sdlgpu is not available (%s); falling back to OpenGL", why.c_str());
        device_.reset();
        if (gl_context_) SDL_GL_DestroyContext(gl_context_);
        gl_context_ = nullptr;
        if (gl_window_) SDL_DestroyWindow(gl_window_);
        gl_window_ = nullptr;
        SDL_DestroyWindow(window_);  // it has no GL pixel format: the GL path makes a new one
        window_ = nullptr;
        return false;
    };

    rhi::SdlGpuDeviceDesc desc;
    desc.driver = "vulkan";
    desc.window = window_;
    auto device = rhi::createSdlGpuDevice(desc);
    if (!device) return fail(device.error());
    device_ = std::move(device.value());
    // On the CPU the GL path over llvmpipe is the one tuned for it (rhi_gl.cpp), not lavapipe.
    if (is_software_adapter(device_->caps().adapter)) return fail("only a CPU device, " + device_->caps().adapter);

    if (params_.legacy_gl_window) {
        int w = params_.width > 0 ? params_.width : 640;
        int h = params_.height > 0 ? params_.height : 480;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        set_gl_context_attributes(false, 0);
        gl_window_ = SDL_CreateWindow("Silent Hill GL", w, h, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (!gl_window_) return fail(SDL_GetError());
        gl_context_ = SDL_GL_CreateContext(gl_window_);
        if (!gl_context_) return fail(SDL_GetError());
    }

    const rhi::Caps& c = device_->caps();
    caps_.adapter = c.adapter;
    caps_.version = c.driver;
    caps_.max_anisotropy = 16.f;  // SDL GPU clamps a sampler's anisotropy to what the device has
    SH_LOG("[GPU] renderer: %s; the game window is presented by %s (%s)%s",
           std::string(backend_description(BackendKind::SdlGpu)).c_str(), c.driver.c_str(), c.adapter.c_str(),
           gl_context_ ? "; legacy GL on a hidden window" : "");
    return true;
}

void DeviceHost::probe_gl_caps() {
    const auto get = gl_fn<GlGetString>("glGetString");
    caps_.vendor = gl_string(get, kGlVendor);
    caps_.adapter = gl_string(get, kGlRenderer);
    caps_.version = gl_string(get, kGlVersion);
    caps_.glsl = gl_string(get, kGlShadingLanguageVersion);
    SH_LOG("[GPU] video adapter: %s by %s", caps_.adapter.c_str(), caps_.vendor.c_str());
    SH_LOG("[GPU] OpenGL version: %s", caps_.version.c_str());
    SH_LOG("[GPU] GLSL version: %s", caps_.glsl.c_str());
    caps_.software = is_software_adapter(caps_.adapter);
    if (caps_.software) SH_LOG("[GPU] %s draws on the CPU", caps_.adapter.c_str());

    // Core profile has no monolithic extension string: walk the indexed list.
    bool aniso = false;
    const auto get_i = gl_fn<GlGetStringi>("glGetStringi");
    const auto get_int = gl_fn<GlGetIntegerv>("glGetIntegerv");
    if (get_i && get_int) {
        int count = 0;
        get_int(kGlNumExtensions, &count);
        for (int i = 0; i < count && !aniso; ++i) {
            const auto* e = reinterpret_cast<const char*>(get_i(kGlExtensions, static_cast<unsigned>(i)));
            aniso = e && std::strstr(e, "texture_filter_anisotropic") != nullptr;
        }
    }
    caps_.max_anisotropy = 0.f;
    if (aniso) {
        if (const auto get_f = gl_fn<GlGetFloatv>("glGetFloatv")) get_f(kGlMaxTextureMaxAnisotropy, &caps_.max_anisotropy);
        SH_LOG("[GPU] anisotropic filtering available, max %.0fx", static_cast<double>(caps_.max_anisotropy));
    }
}

SDL_Window* DeviceHost::window() const { return window_; }

rhi::Device& DeviceHost::device() { return *device_; }

bool DeviceHost::vulkan() const { return device_ && rhi::spirvBackend(device_->backend()); }

BackendKind DeviceHost::backend() const {
    if (!device_) return BackendKind::Gl;
    switch (device_->backend()) {
        case rhi::Backend::Gl33: return BackendKind::Gl;
        case rhi::Backend::Vulkan: return BackendKind::Vulkan;
        case rhi::Backend::SdlGpu: return BackendKind::SdlGpu;
    }
    return BackendKind::Gl;
}

rhi::CommandList& DeviceHost::frame() {
    if (open_) return *open_;
    if (lost_ || dropped_) return *null_list_;
    open_ = device_->begin();
    if (!open_) {
        dropped_ = true;
        note_failure("begin", "the device gave no command list", true);
        return *null_list_;
    }
    return *open_;
}

void DeviceHost::submit() {
    dropped_ = false;
    if (!open_) return;
    open_ = nullptr;
    const auto r = device_->submit();
    if (r) {
        failures_ = 0;
        return;
    }
    // A GL error inside a frame is a bug to log, not a lost device: native GL has nothing to fall back to.
    note_failure("submit", r.error(), vulkan());
}

void DeviceHost::note_failure(const char* where, const std::string& error, bool counts_as_loss) {
    if (failure_logs_++ % kFailureLogEvery == 0) SH_WARN("[GPU] frame %s failed: %s", where, error.c_str());
    if (!counts_as_loss || lost_) return;
    if (++failures_ < kLostAfterFrames) return;
    lost_ = true;
    SH_ERR("[GPU] the %s device stopped working (%s: %s); frames are dropped from now on",
           std::string(backend_name(backend())).c_str(), where, error.c_str());
    if (lost_handler_) lost_handler_(where);
}

void DeviceHost::set_swap_interval(int interval) {
    if (interval == applied_interval_ || !device_) return;
    applied_interval_ = interval;
    if (vulkan()) {
        rhi::sdlGpuSetPresentMode(*device_, interval != 0);
        rhi::vulkanSetPresentMode(*device_, interval != 0);
        return;
    }
    // Adaptive (-1) is an extension: a driver without it gets plain vsync rather than none.
    if (!SDL_GL_SetSwapInterval(interval) && interval < 0) SDL_GL_SetSwapInterval(1);
}

void DeviceHost::reset() { set_swap_interval(0); }

void DeviceHost::window_size(int* w, int* h) const {
    int ww = 0, wh = 0;
    if (window_) SDL_GetWindowSize(window_, &ww, &wh);
    if (w) *w = ww;
    if (h) *h = wh;
}

void DeviceHost::drawable_size(int* w, int* h) const {
    int pw = 0, ph = 0;
    if (window_) SDL_GetWindowSizeInPixels(window_, &pw, &ph);
    if (w) *w = pw;
    if (h) *h = ph;
}

}  // namespace sh::gpu
