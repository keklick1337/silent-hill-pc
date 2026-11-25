#version 450
// The shown frame drawn into the window's present rect (post_pass.cpp, phones): what the blit to the swapchain does,
// as a draw in the render pass the overlay continues, so a tile-based GPU does not write the window to memory and
// read it back. A pixel takes the source point the blit maps its centre to, with the blit's filter (the sampler).

#ifdef SH_RHI_VULKAN
#define SH_UBO(slot) layout(std140, set = 3, binding = slot)
#define SH_SAMPLER(slot) layout(set = 2, binding = slot)
#define SH_VARYING(n) layout(location = n)
#else
#define SH_UBO(slot) layout(std140)
#define SH_SAMPLER(slot)
#define SH_VARYING(n)
#endif

SH_VARYING(0) in vec2 v_uv;

layout(location = 0) out vec4 fragColor;

SH_SAMPLER(0) uniform sampler2D s_scene;

SH_UBO(0) uniform PresentFs {
    vec4 u_src;  // xy: source rect origin, zw: its size (texels, top row first)
    vec4 u_dst;  // xy: present rect origin, zw: its size (pixels, top-left origin)
    vec4 u_win;  // xy: the window's size (pixels)
};

// The window's pre-rotation (post.vert): gl_FragCoord is a pixel of the turned image; WindowPixel() is the window's
// (top-left origin) for a window of u_win.xy pixels.
#ifdef SH_RHI_VULKAN
layout(constant_id = 20) const int kPreRotate = 0;
#else
const int kPreRotate = 0;
#endif
vec2 WindowPixel(vec2 win) {
    if (kPreRotate == 1) return vec2(gl_FragCoord.y, win.y - gl_FragCoord.x);
    if (kPreRotate == 3) return vec2(win.x - gl_FragCoord.y, gl_FragCoord.x);
    return gl_FragCoord.xy;
}

void main() {
    vec2 at = u_src.xy + (WindowPixel(u_win.xy) - u_dst.xy) / u_dst.zw * u_src.zw;
    fragColor = texture(s_scene, at / vec2(textureSize(s_scene, 0)));
}
