// Ambient occlusion and volumetric fog over the world (RENDER.md sections 7.10 and 7.12): the
// settings, and the pass that runs the native Vulkan backend's atmosphere (render/rhi/vk_atmosphere.h) at the end of
// the world layer, before the screen overlay is drawn. Other backends have none; the menu says why.
#pragma once

#include "render/rhi/vk_atmosphere.h"

#include <memory>
#include <string>

namespace sh::render {

// config.cfg: ambient_occlusion, ao_strength, volumetric_fog, fog_scatter, fog_anisotropy. All off by default.
struct AtmosphereSettings {
    int ambient_occlusion = 0;
    float ao_strength = 0.35f;
    int volumetric_fog = 0;
    float fog_scatter = 0.25f;
    float fog_anisotropy = 0.2f;
    // The map's lights and sun without ray tracing (RENDER.md 7.14): map_lights, sun_shadows, god_rays,
    // particle_shadows. All off by default.
    int map_lights = 0;
    int sun_shadows = 0;
    int god_rays = 0;
    int particle_shadows = 0;
};
AtmosphereSettings& atmosphere();

}  // namespace sh::render

namespace sh::gpu {

class Renderer;

class AtmospherePass {
public:
    explicit AtmospherePass(Renderer& owner);
    ~AtmospherePass();
    AtmospherePass(const AtmospherePass&) = delete;
    AtmospherePass& operator=(const AtmospherePass&) = delete;

    // The device can run it; otherwise unavailable() says why (made on first use).
    bool available();
    const std::string& unavailable();
    // A setting asks for it and the device can run it: the scene depth must then be sampled.
    bool wanted();
    // Volumetric fog runs: the scene shades with soft_fog's ramp, which the fog volume reproduces.
    bool volumetric();

    // The world's last scene draw this frame (SplitRenderer): target, projection, fog and flashlight.
    void note_world(const rhi::AtmosphereInputs& in);
    // End of the world layer: the passes over what the world drew, once per frame.
    void apply();

private:
    Renderer& owner_;
    std::unique_ptr<rhi::Atmosphere> impl_;
    bool tried_ = false;
    std::string reason_;
    rhi::AtmosphereInputs world_;
    bool have_world_ = false;
    int failures_ = 0;
};

}  // namespace sh::gpu
