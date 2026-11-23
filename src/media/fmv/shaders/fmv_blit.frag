#version 450
// FMV picture: the decoded frame, or black for the letterbox bars (u_mode.x = 0).

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

SH_SAMPLER(0) uniform sampler2D s_movie;

SH_UBO(0) uniform FmvFs {
    vec4 u_mode;  // x: 1 = sample the frame, 0 = solid black
};

void main() {
    fragColor = u_mode.x > 0.5 ? texture(s_movie, v_uv) : vec4(0.0, 0.0, 0.0, 1.0);
}
