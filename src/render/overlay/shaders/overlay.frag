#version 450
// Overlay quads and lines: vertex colour times the texture (white for flat quads). An alpha-only texture (a font
// atlas, u_mode.x = 1) samples as white with its red channel for alpha.

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
SH_VARYING(1) in vec4 v_color;

layout(location = 0) out vec4 fragColor;

SH_SAMPLER(0) uniform sampler2D s_overlay;

SH_UBO(0) uniform OverlayFs {
    vec4 u_mode;  // x: 1 = alpha-only texture
};

void main() {
    vec4 t = texture(s_overlay, v_uv);
    if (u_mode.x > 0.5) t = vec4(1.0, 1.0, 1.0, t.r);
    fragColor = v_color * t;
}
