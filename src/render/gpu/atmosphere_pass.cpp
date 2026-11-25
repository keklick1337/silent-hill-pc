// AtmospherePass: ambient occlusion and volumetric fog at the end of the world layer (atmosphere_pass.h).

#include "render/gpu/atmosphere_pass.h"

#include "engine/log/sh_log.h"
#include "render/gpu/gpu_types.h"
#include "render/gpu/device_host.h"
#include "render/gpu/renderer.h"
#include "render/gpu/targets.h"

namespace sh::render {

AtmosphereSettings& atmosphere() {
    static AtmosphereSettings s;
    return s;
}

}  // namespace sh::render

namespace sh::gpu {

AtmospherePass::AtmospherePass(Renderer& owner) : owner_(owner) {}

AtmospherePass::~AtmospherePass() = default;

bool AtmospherePass::available() {
    if (!tried_) {
        tried_ = true;
        impl_ = rhi::createAtmosphere(owner_.device(), &reason_, phone_gpu());
        if (impl_)
            SH_LOG("[GPU] ambient occlusion and volumetric fog available");
        else if (owner_.host().backend() == BackendKind::Vulkan)
            SH_LOG("[GPU] ambient occlusion and volumetric fog unavailable: %s", reason_.c_str());
    }
    return impl_ != nullptr;
}

const std::string& AtmospherePass::unavailable() {
    available();
    return reason_;
}

bool AtmospherePass::wanted() {
    const render::AtmosphereSettings& s = render::atmosphere();
    return (s.ambient_occlusion != 0 || s.volumetric_fog != 0) && available();
}

bool AtmospherePass::volumetric() { return render::atmosphere().volumetric_fog != 0 && available(); }

void AtmospherePass::note_world(const rhi::AtmosphereInputs& in) {
    world_ = in;
    have_world_ = true;
}

void AtmospherePass::apply() {
    if (!have_world_) return;
    have_world_ = false;
    if (!wanted() || owner_.targets().samples() > 1) return;
    const render::AtmosphereSettings& s = render::atmosphere();
    rhi::AtmosphereInputs in = world_;
    in.ao = s.ambient_occlusion != 0;
    in.ao_strength = s.ao_strength;
    // A phone's GPU: a sixteenth of the estimates, upsampled by the composite's depth-aware mean of nine (the
    // estimates of 2 cost 2.6 M GPU cycles a frame at 2400x1080 on a Mali-G78).
    if (phone_gpu()) in.ao_step = 4;
    in.volumetric = s.volumetric_fog != 0;
    in.scatter = s.fog_scatter;
    in.anisotropy = s.fog_anisotropy;
    if (auto r = impl_->apply(in); !r && failures_++ < 4) SH_WARN("[GPU] %s", r.error().c_str());
}

}  // namespace sh::gpu
