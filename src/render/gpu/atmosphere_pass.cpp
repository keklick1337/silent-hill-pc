// AtmospherePass: ambient occlusion and volumetric fog at the end of the world layer (atmosphere_pass.h).

#include "render/gpu/atmosphere_pass.h"

#include "engine/log/sh_log.h"
#include "render/gpu/gpu_types.h"
#include "render/gpu/device_host.h"
#include "render/gpu/renderer.h"
#include "render/gpu/targets.h"

#include <algorithm>
#include <cmath>

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
    // A phone's HDR frame: the composite is left to the HDR resolve, which folds it in without a pass that loads
    // and stores the whole scene target. The temporal pass, if it runs, then resolves the picture without it, and
    // the atmosphere goes over its result.
    Targets& targets = owner_.targets();
    in.layer =
        phone_gpu() && in.color == targets.scene_color() && targets.scene_format() == rhi::TextureFormat::RGBA16F;
    if (auto r = impl_->apply(in); !r) {
        if (failures_++ < 4) SH_WARN("[GPU] %s", r.error().c_str());
        return;
    }
    if (const rhi::TextureH layer = in.layer ? impl_->layer() : rhi::TextureH{}) {
        Targets::WorldLayer owed;
        owed.layer = layer;
        owed.layer_z = impl_->layerDepth();
        owed.step = rhi::kLayerStep;
        owed.view = {std::max(0.f, std::floor(in.viewport_x)), std::max(0.f, std::floor(in.viewport_y)),
                     std::floor(in.viewport_w), std::floor(in.viewport_h)};
        owed.sz_max = in.sz_max;
        owed.depth = in.depth;
        owed.apply = [this]() {
            if (auto r = impl_->applyLayer(); !r && failures_++ < 4) SH_WARN("[GPU] %s", r.error().c_str());
        };
        targets.set_world_layer(std::move(owed));
    }
}

}  // namespace sh::gpu
