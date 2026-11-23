// SPDX-License-Identifier: GPL-3.0-or-later
//
// TAA, TAAU and the spatial upscale over the world (RENDER.md section 7.13): the
// jitter of the world's scene draws, the smaller viewport of the world layer while upscaling, the motion of posed
// meshes and moving objects between two drawn frames, and the history's resets. Runs the native Vulkan backend's
// passes (render/rhi/vk_temporal.h) at the end of the world layer, after the atmosphere and before the screen
// overlay, so the HUD, menus and text are drawn over the result at the output resolution. Other backends have none.
#pragma once

#include "render/post/temporal.h"
#include "render/rhi/vk_temporal.h"
#include "render/scene/scene.h"

#include <memory>
#include <string>
#include <vector>

namespace sh::gpu {

class Renderer;

class TemporalPass {
public:
    explicit TemporalPass(Renderer& owner);
    ~TemporalPass();
    TemporalPass(const TemporalPass&) = delete;
    TemporalPass& operator=(const TemporalPass&) = delete;

    bool available();
    const std::string& unavailable();
    // A setting asks for it and the device can run it: the scene depth must then be sampled.
    bool wanted();

    void begin_frame();
    // Start of the world layer: its on-screen draws go into the smaller viewport while upscaling.
    void begin_world();
    // A world scene draw's view, before it draws: the jitter goes onto its projection.
    void jitter(scene::View& view, float viewport_w, float viewport_h) const;
    // The world's scene draw this frame (SplitRenderer): what was drawn, where, with which projection.
    void note_world(const scene::Frame& frame, const scene::View& view, rhi::TextureH color, rhi::TextureH depth,
                    const rhi::Viewport& viewport);
    // The game reads or holds its own frame (feedback effects, freeze frames): those frames are left alone.
    void note_feedback();
    // End of the world layer.
    void apply();

private:
    struct Snapshot {
        bool valid = false;
        int frame = -1;
        post::CameraPose pose;
        float o0 = 0, o5 = 0, o12 = 0, o13 = 0;  // the unjittered ortho's scale and offset
        float draw_ofs_x = 0, draw_ofs_y = 0;
        float geom_ofs_x = 0, geom_ofs_y = 0;
        float sz_max = 65535;
        std::string first_cell;
        std::vector<scene::PrelitMesh> prelit;
        std::vector<scene::ObjectRef> objects;
    };
    std::array<float, 4> proj_of(const Snapshot& s, float h, float gx, float gy) const;
    void build_motion(const Snapshot& now, const Snapshot& before);
    bool engaged() const;

    Renderer& owner_;
    std::unique_ptr<rhi::Temporal> impl_;
    bool tried_ = false;
    std::string reason_;
    int frame_ = 0;
    int jitter_index_ = 0;
    std::array<float, 2> jitter_px_{};  // this frame's jitter in drawn pixels
    bool jittering_ = false;
    float scale_ = 1.0f;       // the world layer's viewport scale this frame
    int feedback_frames_ = 0;  // frames left alone after a feedback effect
    int last_mode_ = 0;
    Snapshot now_, before_;
    bool have_world_ = false;
    rhi::TextureH color_, depth_;
    rhi::Viewport src_{};
    std::vector<rhi::TemporalVertex> motion_;
    int failures_ = 0;
};

}  // namespace sh::gpu
