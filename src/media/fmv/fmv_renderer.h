#pragma once

// The FMV picture on the RHI: one decoded frame, letterboxed into the target at the movie's aspect over black bars.
// Backend-neutral; who owns the target and presents it is the host's business (fmv_present.h).

#include "formats/common/status.h"
#include "render/rhi/rhi.h"
#include "render/rhi/shaders/embedded.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace sh::rhi::embedded {
Shader fmvBlitVert();
Shader fmvBlitFrag();
}  // namespace sh::rhi::embedded

namespace sh::fmv {

class FmvRenderer {
public:
    static Result<std::unique_ptr<FmvRenderer>> create(rhi::Device& device, rhi::TextureFormat colorFormat);
    ~FmvRenderer();
    FmvRenderer(const FmvRenderer&) = delete;
    FmvRenderer& operator=(const FmvRenderer&) = delete;

    // Outside a render pass: the frame (tightly packed RGB24 or RGBA32, top row first) and the quad for a target
    // of targetW x targetH pixels.
    Result<void> upload(rhi::CommandList& cmd, const std::uint8_t* pixels, int width, int height, bool rgba,
                        int targetW, int targetH);
    // Bars, then the picture. setViewport = false when the pass already covers the whole target with no scissor
    // (a GL borrowed pass keeps the caller's viewport).
    void draw(rhi::RenderPass& pass, bool setViewport);
    // Drops the frame texture from the pass's sampler slot, so foreign GL code never samples it.
    void unbind(rhi::RenderPass& pass);

private:
    explicit FmvRenderer(rhi::Device& device) : device_(device) {}

    rhi::Device& device_;
    rhi::ShaderH vs_, fs_;
    rhi::PipelineH pipeline_;
    rhi::SamplerH sampler_;
    rhi::BufferH quad_;
    rhi::TextureH frame_;
    int frameW_ = 0, frameH_ = 0;
    int targetW_ = 0, targetH_ = 0;
    std::vector<std::uint8_t> rgbaScratch_;
};

}  // namespace sh::fmv
