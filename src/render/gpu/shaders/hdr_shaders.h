#pragma once

// HDR pass shaders of sh::gpu::HdrPass, embedded by src/render/gpu/CMakeLists.txt (cmake/rhi_embed_shader.cmake).

#include "render/rhi/shaders/embedded.h"

namespace sh::rhi::embedded {

Shader gpu_hdr_bloom_frag();
Shader gpu_hdr_bloom_up_frag();
Shader gpu_hdr_resolve_frag();
Shader gpu_hdr10_encode_frag();

}  // namespace sh::rhi::embedded
