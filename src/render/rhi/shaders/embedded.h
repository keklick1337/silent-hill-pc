#pragma once

// Built-in shaders, embedded at build time by cmake/rhi_embed_shader.cmake: the GLSL 450 text (for the GL
// backend) and the SPIR-V glslc made from it (for SDL3 GPU).

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace sh::rhi::embedded {

struct Shader {
    std::string_view glsl;
    std::span<const std::uint32_t> spirv;
};

Shader primBatchVert();
Shader primBatchFrag();

}  // namespace sh::rhi::embedded
