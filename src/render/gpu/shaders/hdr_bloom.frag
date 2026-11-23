#version 450
// Bloom of the HDR frame (src/render/gpu/hdr_pass.cpp): a dual filter over a mip chain. Mode 0 takes the light above
// the threshold out of the scene into the first half-size level, mode 1 halves a level again, mode 2 adds the tent
// upsampling of a level onto the one above it (additive blending). Every tap set averages, so the chain keeps the
// energy it was given; the resolve divides the sum of the levels by their count.

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

SH_SAMPLER(0) uniform sampler2D s_src;

SH_UBO(0) uniform BloomFs {
    vec4 u_p;      // x: mode, y: threshold, z: 1 = the source has v = 0 at the top
    vec4 u_texel;  // xy: 1 / source size
};

vec3 src(vec2 uv) {
    vec3 c = texture(s_src, u_p.z > 0.5 ? vec2(uv.x, 1.0 - uv.y) : uv).rgb;
    return u_p.x < 0.5 ? max(c - vec3(u_p.y), vec3(0.0)) : c;
}

void main() {
    vec2 t = u_texel.xy;
    vec2 uv = v_uv;
    vec3 c;
    if (u_p.x < 1.5) {
        c = src(uv) * 4.0 + src(uv + vec2(-t.x, -t.y)) + src(uv + vec2(t.x, -t.y)) + src(uv + vec2(-t.x, t.y)) +
            src(uv + vec2(t.x, t.y));
        c *= 1.0 / 8.0;
    } else {
        c = src(uv) * 4.0 +
            (src(uv + vec2(-t.x, 0.0)) + src(uv + vec2(t.x, 0.0)) + src(uv + vec2(0.0, -t.y)) + src(uv + vec2(0.0, t.y))) *
                2.0 +
            src(uv + vec2(-t.x, -t.y)) + src(uv + vec2(t.x, -t.y)) + src(uv + vec2(-t.x, t.y)) + src(uv + vec2(t.x, t.y));
        c *= 1.0 / 16.0;
    }
    fragColor = vec4(c, 1.0);
}
