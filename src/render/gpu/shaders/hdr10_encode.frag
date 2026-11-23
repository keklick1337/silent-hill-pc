#version 450
// HDR10 output (src/render/gpu/hdr_pass.cpp): the composed window picture, in the frame's encoding with 1.0 = SDR
// white, to SMPTE ST 2084 over BT.2020 for a swapchain in VK_COLOR_SPACE_HDR10_ST2084_EXT.

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

SH_UBO(0) uniform EncodeFs {
    vec4 u_p;  // x: paper white nits, y: peak nits, z: flip v
};

vec3 srgbDecode(vec3 e) {
    e = max(e, vec3(0.0));
    return mix(pow((e + 0.055) / 1.055, vec3(2.4)), e / 12.92, lessThanEqual(e, vec3(0.04045)));
}

vec3 pq(vec3 nits) {
    const float m1 = 2610.0 / 16384.0, m2 = 2523.0 / 4096.0 * 128.0;
    const float c1 = 3424.0 / 4096.0, c2 = 2413.0 / 4096.0 * 32.0, c3 = 2392.0 / 4096.0 * 32.0;
    vec3 y = pow(clamp(nits / 10000.0, 0.0, 1.0), vec3(m1));
    return pow((c1 + c2 * y) / (1.0 + c3 * y), vec3(m2));
}

void main() {
    vec2 uv = u_p.z > 0.5 ? vec2(v_uv.x, 1.0 - v_uv.y) : v_uv;
    vec3 l = srgbDecode(texture(s_src, uv).rgb) * u_p.x;
    // BT.709 primaries to BT.2020 (ITU-R BT.2087).
    vec3 w = vec3(dot(l, vec3(0.6274, 0.3293, 0.0433)), dot(l, vec3(0.0691, 0.9195, 0.0114)),
                  dot(l, vec3(0.0164, 0.0880, 0.8956)));
    fragColor = vec4(pq(min(w, vec3(u_p.y))), 1.0);
}
