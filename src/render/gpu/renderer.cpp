// sh::gpu::Renderer: wires the zones of the primitive renderer together (renderer.h).

#include "render/gpu/renderer.h"

#include "render/gpu/atmosphere_pass.h"
#include "render/gpu/temporal_pass.h"
#include "engine/log/sh_log.h"
#include "render/gpu/device_host.h"
#include "render/gpu/feedback_pass.h"
#include "render/gpu/post_pass.h"
#include "render/gpu/shadow_pass.h"
#include "render/gpu/sun_shadow_pass.h"
#include "render/gpu/split_builder.h"
#include "render/gpu/split_renderer.h"
#include "render/gpu/targets.h"
#include "render/gpu/textures.h"
#include "engine/devtools/perf_monitor.h"
#include "render/gpu/vram.h"
#include "render/rhi/prim_batch.h"
#include "render/rhi/rhi_vulkan.h"

namespace sh::gpu {

Renderer::Renderer() = default;

Renderer::~Renderer() {
    if (host_) host_->device().waitIdle();
    // Everything below holds device objects: release it while the device lives, users before what they use.
    temporal_.reset();
    atmosphere_.reset();
    splits_.reset();
    post_.reset();
    shadow_.reset();
    sun_shadow_.reset();
    feedback_.reset();
    prims_.reset();
    targets_.reset();
    textures_.reset();
    if (vram_ && host_) vram_->release(host_->device());
    vram_.reset();
    host_.reset();
}

std::unique_ptr<Renderer> Renderer::create(const InitParams& params) {
    std::unique_ptr<Renderer> r(new Renderer());
    r->host_ = DeviceHost::create(params);
    if (!r->host_) {
        SH_WARN("[GPU] no device: the primitive renderer is not available");
        return nullptr;
    }
    rhi::Device& dev = r->host_->device();

    r->vram_ = std::make_unique<Vram>();
    if (auto v = r->vram_->create_texture(dev); !v) {
        SH_WARN("[GPU] video memory texture: %s", v.error().c_str());
        return nullptr;
    }
    r->textures_ = std::make_unique<Textures>(dev);
    r->targets_ = std::make_unique<Targets>(dev);
    // The first scene target already takes the configured MSAA, so it is not built twice at startup.
    r->targets_->view().msaa_samples = r->host_->msaa_samples();
    r->host_->window_size(&r->window_w_, &r->window_h_);
    r->targets_->resize(r->window_w_, r->window_h_);

    auto prims = rhi::PrimRenderer::create(dev, rhi::TextureFormat::RGBA8, true, SplitBuilder::kMaxVertices);
    if (!prims) {
        SH_WARN("[GPU] PS1 primitive renderer: %s", prims.error().c_str());
        return nullptr;
    }
    r->prims_ = std::move(prims.value());
    // The shaders sample the engine's video memory, not the prim renderer's own texture.
    r->prims_->useVram(r->vram_->texture());

    r->splits_ = std::make_unique<SplitRenderer>(*r);
    r->feedback_ = std::make_unique<FeedbackPass>(*r);
    r->shadow_ = std::make_unique<ShadowPass>(*r);
    r->sun_shadow_ = std::make_unique<SunShadowPass>(*r);
    r->post_ = std::make_unique<PostPass>(*r);
    r->atmosphere_ = std::make_unique<AtmospherePass>(*r);
    r->temporal_ = std::make_unique<TemporalPass>(*r);

    SplitRenderer* splits = r->splits_.get();
    SceneCasters casters;
    casters.prepare = [splits](rhi::CommandList& cmd) { splits->prepare_scene(cmd); };
    casters.draw = [splits](rhi::RenderPass& pass, std::int32_t item, const std::array<float, 16>& light, float z_far,
                            bool tint) {
        splits->draw_scene_casters(pass, item, light, z_far, tint);
    };
    r->shadow_->set_scene_casters(std::move(casters));

    FeedbackPass* feedback = r->feedback_.get();
    r->vram_->on_change([feedback](const Rect& rect) { feedback->note_vram_upload(rect); });

    SH_LOG("[GPU] primitive renderer: %s (%s)", dev.caps().driver.c_str(), dev.caps().adapter.c_str());
    return r;
}

rhi::Device& Renderer::device() { return host_->device(); }

rhi::PrimRenderer& Renderer::prims() { return *prims_; }

void Renderer::begin_frame() {
    int w = 0, h = 0;
    host_->window_size(&w, &h);
    targets_->set_sampled_depth(atmosphere_->wanted() || temporal_->wanted());
    temporal_->begin_frame();
    const post::TemporalSettings& pacing = post::temporal();
    rhi::vulkanSetFramePacing(host_->device(),
                              {pacing.present_mode, pacing.frames_in_flight, pacing.present_wait != 0});
    if (w != window_w_ || h != window_h_ || targets_->needs_resize()) {
        window_w_ = w;
        window_h_ = h;
        targets_->resize(w, h);
    }
    scene_ended_ = false;
    (void)host_->frame();
}

void Renderer::draw(SplitBatch& batch) {
    if (batch.splits.empty()) return;
    rhi::CommandList& cmd = host_->frame();
    {
        SH_PERF_SCOPE("render_uploads", "world_list_draw");
        if (vram_->dirty()) vram_->upload(cmd);
        textures_->flush(cmd);
        // The shadow pre-pass reads the batch's vertex stream, so it goes up first.
        splits_->upload(cmd, batch);
    }
    {
        SH_PERF_SCOPE("render_shadow_pass", "world_list_draw");
        shadow_->set_map_light(splits_->map_light_shadow());
        shadow_->build(cmd, batch);
        sun_shadow_->build(cmd, batch);
    }
    SH_PERF_SCOPE("render_batch_and_scene_draw", "world_list_draw");
    splits_->draw(cmd, batch);
}

void Renderer::clear(const Rect& vram_rect, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    targets_->clear(host_->frame(), vram_rect, r, g, b);
}

void Renderer::clear_depth() { targets_->clear_depth(host_->frame()); }

void Renderer::begin_world() { temporal_->begin_world(); }

void Renderer::end_world() {
    (void)host_->frame();
    atmosphere_->apply();
    temporal_->apply();
}

void Renderer::end_scene() {
    if (scene_ended_) return;
    scene_ended_ = true;
    rhi::CommandList& cmd = host_->frame();
    // A video-memory change after the last flush still has to reach the texture the packed frame lands in.
    SH_PERF_SCOPE("render_frame_store", "end_scene");
    if (vram_->dirty()) vram_->upload(cmd);
    feedback_->store_frame(cmd);
    feedback_->capture_last_frame(cmd);
}

void Renderer::present() {
    end_scene();
    textures_->flush(host_->frame());
    {
        SH_PERF_SCOPE("render_post_passes", "end_scene");
        post_->present(host_->frame());
    }
    // The buffer swap: with vsync on, the wait for the display is here.
    SH_PERF_SCOPE("render_submit_and_swap", "end_scene");
    host_->submit();
    scene_ended_ = false;
}

void Renderer::present_picture() {
    scene_ended_ = true;
    present();
}

void Renderer::set_draw_env(const DrawEnv& env) { draw_env_ = env; }

void Renderer::set_disp_env(const DispEnv& env) { disp_env_ = env; }

void Renderer::load_image(const Rect& rect, const std::uint16_t* words) {
    vram_->load(rect, words);
    vram_->mark_dirty(rect);
}

void Renderer::store_image(const Rect& rect, std::uint16_t* words) { vram_->store(rect, words); }

bool Renderer::frame_size(int* w, int* h) const { return post_->frame_size(w, h); }

int Renderer::read_frame_rgba(std::uint8_t* out, int max_bytes) { return post_->read_frame_rgba(out, max_bytes); }

}  // namespace sh::gpu
