#pragma once

// Feedback pass shaders of sh::gpu::FeedbackGpu, embedded by src/render/gpu/CMakeLists.txt
// (cmake/rhi_embed_shader.cmake).

#include "render/rhi/shaders/embedded.h"

namespace sh::rhi::embedded {

Shader gpu_feedback_quad_vert();
Shader gpu_feedback_capture_frag();
Shader gpu_feedback_pack_frag();

}  // namespace sh::rhi::embedded
