#version 450
// Upsampling step of the bloom chain on phones (src/render/gpu/hdr_pass.cpp): the level's own downsample plus the tent
// upsampling of the level below, written into a fresh target. hdr_bloom.frag mode 2 adds the same tent onto the
// level by blending instead, which on a tiler means loading the target from memory first.

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
SH_SAMPLER(1) uniform sampler2D s_base;

SH_UBO(0) uniform BloomUpFs {
    vec4 u_p;      // x: weight of the level below (the levels it stands for), z: 1 = the source has v = 0 at the top
    vec4 u_texel;  // xy: 1 / size of the level below
};

vec2 flipV(vec2 uv) { return u_p.z > 0.5 ? vec2(uv.x, 1.0 - uv.y) : uv; }

vec3 src(vec2 uv) { return texture(s_src, flipV(uv)).rgb; }

void main() {
    vec2 t = u_texel.xy;
    vec2 uv = v_uv;
    vec3 c = src(uv) * 4.0;
    c += (src(uv + vec2(-t.x, 0.0)) + src(uv + vec2(t.x, 0.0)) + src(uv + vec2(0.0, -t.y)) + src(uv + vec2(0.0, t.y))) *
         2.0;
    c += src(uv + vec2(-t.x, -t.y)) + src(uv + vec2(t.x, -t.y)) + src(uv + vec2(-t.x, t.y)) + src(uv + vec2(t.x, t.y));
    fragColor = vec4(texture(s_base, flipV(uv)).rgb + c * (u_p.x / 16.0), 1.0);
}
