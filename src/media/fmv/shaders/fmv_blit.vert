#version 450
// FMV picture: a screen-space quad in NDC with its texture coordinates. One source for both RHI backends
// (glslc -DSH_RHI_VULKAN for SDL3 GPU; the GL backend swaps the #version line).

#ifdef SH_RHI_VULKAN
#define SH_VARYING(n) layout(location = n)
#else
#define SH_VARYING(n)
#endif

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;

SH_VARYING(0) out vec2 v_uv;

void main() {
    v_uv = a_uv;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
