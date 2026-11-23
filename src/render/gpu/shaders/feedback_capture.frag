#version 450
// Capture of the scene target into a feedback texture (sh::gpu::FeedbackGpu::capture), a
// window -> texture blit: destination texel rows count from the bottom of the area on every backend, so the
// capture has the layout prim_batch.frag's feedback sampling reads. Texels outside the destination rect are black.

#ifdef SH_RHI_VULKAN
#define SH_UBO(slot) layout(std140, set = 3, binding = slot)
#define SH_SAMPLER(slot) layout(set = 2, binding = slot)
#else
#define SH_UBO(slot) layout(std140)
#define SH_SAMPLER(slot)
#endif

layout(location = 0) out vec4 fragColor;

SH_SAMPLER(0) uniform sampler2D s_src;

SH_UBO(0) uniform FeedbackCaptureFs {
    vec4 u_src;   // source rect x0 y0 x1 y1, scene pixels with a bottom-left origin
    vec4 u_dst;   // destination rect x0 y0 x1 y1, capture texels (row 0 = bottom)
    vec4 u_size;  // xy: scene target size, z: 1 = the scene's storage rows run top down (SDL3 GPU)
};

void main() {
    vec2 p = floor(gl_FragCoord.xy);
    if (p.x < u_dst.x || p.y < u_dst.y || p.x >= u_dst.z || p.y >= u_dst.w) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec2 s = u_src.xy + (p + 0.5 - u_dst.xy) * (u_src.zw - u_src.xy) / (u_dst.zw - u_dst.xy);
    if (u_size.z > 0.5) s.y = u_size.y - s.y;
    fragColor = vec4(texture(s_src, s / u_size.xy).rgb, 1.0);
}
