#pragma once

// The MSAA seed pass of sh::gpu::Targets, embedded by src/render/gpu/CMakeLists.txt (cmake/rhi_embed_shader.cmake).
// Its vertex stage is the feedback passes' full-target triangle.

#include "render/rhi/shaders/embedded.h"

namespace sh::rhi::embedded {

Shader gpu_feedback_quad_vert();
Shader gpu_ms_seed_frag();

}  // namespace sh::rhi::embedded
