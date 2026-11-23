#version 450
// Packs RGBA into 16-bit PS1 words for the RG8 video-memory texture (sh::gpu::FeedbackGpu::pack): R = low byte,
// G = high byte, bits 0-4 red, 5-9 green, 10-14 blue, 15 the STP/mask bit. Destination rows are video-memory rows
// top first (gl_FragCoord rows are storage rows on both backends). Black packs to word 0, the transparent texel;
// with the mask bit wanted, every non-zero word gets it.
//   Scaled:   level = floor(c * gain * 31)                (the damped loop of a blending reader)
//   Exact:    level = round(c * 31.875) * gain, - decay   (the PS1 copy loop: identity, or one level on 127/128)
//   Truncate: level = byte >> 3                           (an offscreen area written back as the GPU stores it)

#ifdef SH_RHI_VULKAN
#define SH_UBO(slot) layout(std140, set = 3, binding = slot)
#define SH_SAMPLER(slot) layout(set = 2, binding = slot)
#else
#define SH_UBO(slot) layout(std140)
#define SH_SAMPLER(slot)
#endif

layout(location = 0) out vec4 fragColor;

SH_SAMPLER(0) uniform sampler2D s_src;

SH_UBO(0) uniform FeedbackPackFs {
    vec4 u_src;      // source rect x y w h in storage texels
    vec4 u_srcInfo;  // xy: source size, z: 1 = the source's first row is the bottom of the rect
    vec4 u_dst;      // xy: destination size in words
    vec4 u_params;   // x: gain, y: mask bit, z: mode (0 scaled, 1 exact, 2 truncate), w: decay
};

void main() {
    vec2 p = floor(gl_FragCoord.xy);
    float sx = u_src.x + (p.x + 0.5) * u_src.z / u_dst.x;
    float r = (p.y + 0.5) * u_src.w / u_dst.y;
    float sy = (u_srcInfo.z > 0.5) ? u_src.y + u_src.w - r : u_src.y + r;
    vec3 c = texture(s_src, vec2(sx, sy) / u_srcInfo.xy).rgb;

    int mode = int(u_params.z + 0.5);
    vec3 level;
    if (mode == 1) {
        level = clamp(floor(c * 31.875 + 0.5), 0.0, 31.0);
        level = floor(level * u_params.x + 0.001);
        level = max(level - vec3(u_params.w), vec3(0.0));
    } else if (mode == 2) {
        level = floor(floor(c * 255.0 + 0.5) / 8.0);
    } else {
        level = clamp(floor(c * u_params.x * 31.0 + 0.002), 0.0, 31.0);
    }
    float word = level.r + level.g * 32.0 + level.b * 1024.0;
    if (u_params.y > 0.5 && word > 0.0) word += 32768.0;
    float hi = floor(word / 256.0);
    float lo = word - hi * 256.0;
    fragColor = vec4(lo / 255.0, hi / 255.0, 0.0, 1.0);
}
