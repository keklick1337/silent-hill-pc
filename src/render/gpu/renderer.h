// The engine's primitive renderer on the RHI (docs/RENDER.md section 3): it owns the window's device, video
// memory as RHI textures, the scene/offscreen/post targets, the vertex stream, the shader programs and the feedback
// and shadow passes. The front end hands it SplitBatch flushes.
//
// Each component below is one zone of work with its own header; Renderer only wires them together.
#pragma once

#include "render/gpu/backend_name.h"
#include "render/gpu/gpu_types.h"

#include <cstdint>
#include <memory>

struct SDL_Window;

namespace sh::rhi {
class Device;
class CommandList;
class RenderPass;
class PrimRenderer;
}  // namespace sh::rhi

namespace sh::gpu {

class DeviceHost;     // device_host.h: window, GL context or SDL3 GPU device, swap interval, frame begin/submit
class Vram;           // vram.h: the 1024x512 video memory (CPU words) and its RHI texture mirror
class Textures;       // textures.h: white texture, hi-res override and RGBA textures as RHI handles by registry id
class Targets;        // targets.h: internal-resolution scene colour+depth/stencil, offscreen redirect, present rect
class SplitRenderer;  // split_renderer.h: draws a SplitBatch (state per split, item depth pass, scene/mesh/capture)
class FeedbackPass;   // feedback_pass.h: frame captures packed into video memory, previous-frame sampling
class ShadowPass;     // shadow_pass.h: flashlight shadow map from opaque splits and scene casters
class PostPass;       // post_pass.h: post-processing and the final present to the swapchain
class SunShadowPass;  // sun_shadow_pass.h: the sun's shadow map without ray tracing (native Vulkan)
class AtmospherePass;  // atmosphere_pass.h: ambient occlusion and volumetric fog over the world (native Vulkan)
class TemporalPass;    // temporal_pass.h: TAA, TAAU and the spatial upscale of the world (native Vulkan)

struct InitParams {
    const char* window_name = "Silent Hill";
    int width = 640, height = 480;
    int fullscreen = 0;   // 0 windowed, 1 exclusive, 2 borderless
    int msaa = 0;         // scene target samples asked for (snapped to 0/2/4/8)
    BackendKind backend = BackendKind::Gl;  // the `renderer` key
    bool vulkan_validation = false;         // `vulkan_validation`: the Khronos layer when installed
    int software_gl = 0;                    // `software_gl`: sh::gpu::SoftwareGl
};

class Renderer {
public:
    static std::unique_ptr<Renderer> create(const InitParams& params);  // nullptr on failure (logged)
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Frame
    void begin_frame();
    void draw(SplitBatch& batch);  // one flush: shadow pre-pass, then every split in order
    void clear(const Rect& vram_rect, std::uint8_t r, std::uint8_t g, std::uint8_t b);  // a clear of the draw area
    void clear_depth();  // depth and stencil of the scene target only
    // Start of the world layer: its on-screen draws go into the upscaler's smaller viewport (when on).
    void begin_world();
    // End of the world layer: ambient occlusion, volumetric fog, then TAA or the upscale over what it drew (when on
    // and available).
    void end_world();
    // End of the composed frame: store it for next frame's feedback reads and keep it for freeze frames
    // Overlays that must not be in those copies (the console) draw
    // after it. present() runs it when the frame did not.
    void end_scene();
    void present();                // post-processing, present, swap; Frame::end_frame
    // A picture that is not a game frame (a movie): presented without the frame store and the freeze capture, so
    // the video memory and the frozen frame keep the game's last frame.
    void present_picture();

    // Environment as the game last set it (PutDrawEnv / PutDispEnv).
    void set_draw_env(const DrawEnv& env);
    void set_disp_env(const DispEnv& env);
    const DrawEnv& draw_env() const { return draw_env_; }
    const DispEnv& disp_env() const { return disp_env_; }

    // Video memory transfers (LoadImage, StoreImage, MoveImage, ClearImage).
    void load_image(const Rect& rect, const std::uint16_t* words);
    void store_image(const Rect& rect, std::uint16_t* words);

    // Screenshots and tests: the presented frame as RGBA8, top row first.
    bool frame_size(int* w, int* h) const;
    int read_frame_rgba(std::uint8_t* out, int max_bytes);

    DeviceHost& host() { return *host_; }
    Vram& vram() { return *vram_; }
    Textures& textures() { return *textures_; }
    Targets& targets() { return *targets_; }
    SplitRenderer& splits() { return *splits_; }
    FeedbackPass& feedback() { return *feedback_; }
    ShadowPass& shadow() { return *shadow_; }
    SunShadowPass& sun_shadow() { return *sun_shadow_; }
    PostPass& post() { return *post_; }
    AtmospherePass& atmosphere() { return *atmosphere_; }
    TemporalPass& temporal() { return *temporal_; }
    rhi::Device& device();
    rhi::PrimRenderer& prims();

private:
    Renderer();
    std::unique_ptr<DeviceHost> host_;
    std::unique_ptr<Vram> vram_;
    std::unique_ptr<Textures> textures_;
    std::unique_ptr<Targets> targets_;
    std::unique_ptr<SplitRenderer> splits_;
    std::unique_ptr<FeedbackPass> feedback_;
    std::unique_ptr<ShadowPass> shadow_;
    std::unique_ptr<SunShadowPass> sun_shadow_;
    std::unique_ptr<PostPass> post_;
    std::unique_ptr<AtmospherePass> atmosphere_;
    std::unique_ptr<TemporalPass> temporal_;
    std::unique_ptr<rhi::PrimRenderer> prims_;  // prim_batch program, pipelines, vertex stream
    DrawEnv draw_env_;
    DispEnv disp_env_;
    int window_w_ = 0, window_h_ = 0;
    bool scene_ended_ = false;
};

// The renderer of the running game; null before the bridge created it and in unit tests. The creator owns it.
Renderer* renderer();
void set_renderer(Renderer* r);

}  // namespace sh::gpu
