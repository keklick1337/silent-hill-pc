#pragma once

// Post pass shaders of sh::gpu::PostPass, embedded by src/render/gpu/CMakeLists.txt (cmake/rhi_embed_shader.cmake).

#include "render/rhi/shaders/embedded.h"

namespace sh::rhi::embedded {

Shader gpu_post_vert();
Shader gpu_post_frag();

}  // namespace sh::rhi::embedded
