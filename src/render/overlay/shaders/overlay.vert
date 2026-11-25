#version 450
// Overlay quads and lines: vertices in target pixels (top-left origin, +Y down) to GL clip space. One source for
// both RHI backends (glslc -DSH_RHI_VULKAN for SDL3 GPU; the GL backend swaps the #version line).

#ifdef SH_RHI_VULKAN
#define SH_UBO(slot) layout(std140, set = 1, binding = slot)
#define SH_VARYING(n) layout(location = n)
#else
#define SH_UBO(slot) layout(std140)
#define SH_VARYING(n)
#endif

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;

SH_VARYING(0) out vec2 v_uv;
SH_VARYING(1) out vec4 v_color;

// The window's pre-rotation (rhi_vulkan.cpp, VkTexture::rotation): set for draws into a swapchain image made in the
// screen's own orientation, a quarter turn of NDC; 0 everywhere else.
#ifdef SH_RHI_VULKAN
layout(constant_id = 20) const int kPreRotate = 0;
#else
const int kPreRotate = 0;
#endif
vec4 PreRotate(vec4 p) {
    if (kPreRotate == 1) return vec4(p.y, -p.x, p.zw);
    if (kPreRotate == 3) return vec4(-p.y, p.x, p.zw);
    return p;
}

SH_UBO(0) uniform OverlayVs {
    vec4 u_target;  // xy: target size in pixels
};

void main() {
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = PreRotate(vec4(a_pos.x / u_target.x * 2.0 - 1.0, 1.0 - a_pos.y / u_target.y * 2.0, 0.0, 1.0));
}
