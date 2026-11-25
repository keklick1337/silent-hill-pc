#version 450
// Full-screen post triangle (src/render/gpu/post_pass.cpp). v_uv keeps the GL convention on both backends:
// v = 0 at the bottom of the screen, so the looks that depend on uv.y (scanline phase, the calibration bar) land
// in the same place. One source for both RHI backends.

#ifdef SH_RHI_VULKAN
#define SH_VARYING(n) layout(location = n)
#else
#define SH_VARYING(n)
#endif

layout(location = 0) in vec2 a_pos;

SH_VARYING(0) out vec2 v_uv;

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

void main() {
    v_uv = (a_pos + 1.0) * 0.5;
    gl_Position = PreRotate(vec4(a_pos, 0.0, 1.0));
}
