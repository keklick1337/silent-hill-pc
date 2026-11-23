// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Off-screen scene pictures (offscreen_scene.h): a scene renderer of its own on the window's device, a colour and a
// depth target, one command list of its own after the frame's present, then a synchronous readback.

#include "render/gpu/offscreen_scene.h"

#include "engine/log/sh_log.h"
#include "render/gpu/device_host.h"
#include "render/gpu/frame_driver.h"
#include "render/gpu/renderer.h"
#include "render/rhi/prim_batch.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <span>

namespace sh::gpu {
namespace {

struct State {
    std::optional<OffscreenScene> job;
    std::vector<std::uint8_t> picture;
    int picture_w = 0, picture_h = 0;
    bool fresh = false;
    bool listening = false;
    rhi::Device* device = nullptr;
    std::unique_ptr<scene::SceneRenderer> scene;
    rhi::TextureH color, depth;
    int target_w = 0, target_h = 0;
    int failures = 0;
};

State& state() {
    static State* s = new State;  // the device's objects are released with it, not at static destruction
    return *s;
}

void release_targets(State& s) {
    if (s.device == nullptr) return;
    if (s.color) s.device->destroy(s.color);
    if (s.depth) s.device->destroy(s.depth);
    s.color = {};
    s.depth = {};
    s.target_w = s.target_h = 0;
}

bool ensure_targets(State& s, int w, int h) {
    if (s.color && s.target_w == w && s.target_h == h) return true;
    release_targets(s);
    const auto uw = static_cast<std::uint32_t>(w), uh = static_cast<std::uint32_t>(h);
    auto c = s.device->createTexture({rhi::TextureFormat::RGBA8, uw, uh, rhi::kUsageSampled | rhi::kUsageColorTarget,
                                      "offscreen scene colour"});
    auto d = s.device->createTexture(
        {rhi::TextureFormat::D24S8, uw, uh, rhi::kUsageDepthStencilTarget, "offscreen scene depth"});
    if (!c || !d) {
        if (c) s.device->destroy(c.value());
        if (d) s.device->destroy(d.value());
        return false;
    }
    s.color = c.value();
    s.depth = d.value();
    s.target_w = w;
    s.target_h = h;
    return true;
}

void fail(State& s, const std::string& what) {
    if (s.failures++ < 4) SH_WARN("[GPU] offscreen scene: %s", what.c_str());
}

void run() {
    State& s = state();
    if (!s.job) return;
    OffscreenScene job = std::move(*s.job);
    s.job.reset();
    Renderer* r = renderer();
    if (r == nullptr || r->host().lost() || r->host().frame_open()) return;
    rhi::Device& device = r->device();
    if (s.device != &device) {
        // A new device (renderer switch): the old one took its objects with it, so the old renderer is let go
        // without its destructor (which would release them on the dead device).
        (void)s.scene.release();
        s.color = s.depth = {};
        s.target_w = s.target_h = 0;
        s.device = &device;
    }
    if (!s.scene) {
        auto made = scene::SceneRenderer::create(device, rhi::TextureFormat::RGBA8);
        if (!made) return fail(s, made.error());
        s.scene = std::move(made.value());
    }
    const int w = std::clamp(job.width, 16, 2048), h = std::clamp(job.height, 16, 2048);
    if (!ensure_targets(s, w, h)) return fail(s, "targets");

    rhi::CommandList* cmd = device.begin();
    if (cmd == nullptr) return;
    job.frame.serial = ~0ull - static_cast<std::uint64_t>(s.failures);  // never one of the game's frames
    if (auto p = s.scene->prepare(*cmd, job.frame); !p) {
        (void)device.submit();
        return fail(s, p.error());
    }
    rhi::RenderPassDesc desc;
    desc.color = {s.color, rhi::LoadOp::Clear, rhi::StoreOp::Store, job.clear};
    desc.depthStencil = {s.depth, rhi::LoadOp::Clear, rhi::StoreOp::DontCare, 1.f, 0};
    desc.name = "preview";
    rhi::RenderPass* pass = cmd->beginRenderPass(desc);
    if (pass == nullptr) {
        (void)device.submit();
        return fail(s, "render pass");
    }
    pass->setViewport({0.f, 0.f, static_cast<float>(w), static_cast<float>(h), 0.f, 1.f});

    scene::View view;
    view.ortho = rhi::orthoProjection(0.f, static_cast<float>(w), static_cast<float>(h), 0.f, -1.f, 1.f);
    view.sz_max = job.depth_far;
    view.pixel_w = static_cast<float>(w);
    view.pixel_h = static_cast<float>(h);
    view.fs.fog[3] = 0.0f;

    // The editor's world overrides are not this picture's: only its own wireframe applies while it is drawn.
    scene::ViewerOverrides kept = scene::viewer_overrides();
    scene::viewer_overrides() = scene::ViewerOverrides{};
    scene::viewer_overrides().active = job.wireframe;
    scene::viewer_overrides().wireframe = job.wireframe;
    (void)s.scene->draw(*pass, job.frame, view, -1);
    for (std::size_t i = 0; i < job.frame.prelit.size(); ++i) (void)s.scene->draw(*pass, job.frame, view, static_cast<int>(i));
    for (std::size_t i = 0; i < job.frame.objects.size(); ++i)
        (void)s.scene->draw(*pass, job.frame, view, scene::kObjectItem + static_cast<int>(i));
    scene::viewer_overrides() = std::move(kept);
    cmd->endRenderPass();
    if (auto sub = device.submit(); !sub) return fail(s, sub.error());

    s.picture.resize(static_cast<std::size_t>(w) * h * 4);
    if (auto rb = device.readback(s.color, {0, 0, w, h}, std::as_writable_bytes(std::span(s.picture))); !rb)
        return fail(s, rb.error());
    for (std::size_t i = 3; i < s.picture.size(); i += 4) s.picture[i] = 0xFF;
    s.picture_w = w;
    s.picture_h = h;
    s.fresh = true;
}

}  // namespace

void request_offscreen_scene(OffscreenScene job) {
    State& s = state();
    s.job = std::move(job);
    if (!s.listening) {
        add_frame_end_listener(&run);
        s.listening = true;
    }
}

bool take_offscreen_picture(std::vector<std::uint8_t>* rgba, int* width, int* height) {
    State& s = state();
    if (!s.fresh) return false;
    s.fresh = false;
    if (rgba) *rgba = s.picture;
    if (width) *width = s.picture_w;
    if (height) *height = s.picture_h;
    return true;
}

}  // namespace sh::gpu
