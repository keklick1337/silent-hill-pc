#version 450
// Resolve of the HDR frame (src/render/gpu/hdr_pass.cpp): the bloom put back energy-conserving (the light above the
// threshold leaves its pixel and returns spread by the chain), then the tonemapper into the display's range. The
// operators match sh::post::tonemapRgb (src/render/post/hdr.cpp).

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
SH_SAMPLER(1) uniform sampler2D s_bloom;

SH_UBO(0) uniform ResolveFs {
    vec4 u_mode;   // x: operator (0 clamp, 1 soft, 2 ACES, 3 Reinhard), y: bloom share, z: threshold, w: flip v
    vec4 u_out;    // x: white (largest value written), y: 1 / bloom levels
    vec4 u_texel;  // zw: 1 / size of the first bloom level
};

vec2 flipV(vec2 uv) { return u_mode.w > 0.5 ? vec2(uv.x, 1.0 - uv.y) : uv; }

vec3 srgbEncode(vec3 l) {
    l = max(l, vec3(0.0));
    return mix(1.055 * pow(l, vec3(1.0 / 2.4)) - 0.055, l * 12.92, lessThanEqual(l, vec3(0.0031308)));
}

vec3 srgbDecode(vec3 e) {
    e = max(e, vec3(0.0));
    return mix(pow((e + 0.055) / 1.055, vec3(2.4)), e / 12.92, lessThanEqual(e, vec3(0.04045)));
}

vec3 bloomAt(vec2 uv) { return texture(s_bloom, flipV(uv)).rgb; }

vec3 bloomTent(vec2 uv) {
    vec2 t = u_texel.zw;
    vec3 c = bloomAt(uv) * 4.0;
    c += (bloomAt(uv + vec2(-t.x, 0.0)) + bloomAt(uv + vec2(t.x, 0.0)) + bloomAt(uv + vec2(0.0, -t.y)) +
          bloomAt(uv + vec2(0.0, t.y))) * 2.0;
    c += bloomAt(uv + vec2(-t.x, -t.y)) + bloomAt(uv + vec2(t.x, -t.y)) + bloomAt(uv + vec2(-t.x, t.y)) +
         bloomAt(uv + vec2(t.x, t.y));
    return c * (1.0 / 16.0);
}

vec3 tonemap(vec3 c, int op, float white) {
    if (op == 1) {
        float knee = 0.85 * white, room = white - knee;
        float pk = max(c.r, max(c.g, c.b));
        if (pk > knee) c *= (knee + room * (1.0 - exp((knee - pk) / room))) / pk;
    } else if (op == 2) {
        vec3 x = srgbDecode(c) * 0.6;
        c = srgbEncode(clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0));
    } else if (op == 3) {
        vec3 l = srgbDecode(c);
        float y = dot(l, vec3(0.2126, 0.7152, 0.0722));
        if (y > 0.0) c = srgbEncode(l * ((1.0 + y / 16.0) / (1.0 + y)));
    }
    return min(c, vec3(white));
}

void main() {
    vec3 c = max(texture(s_scene, flipV(v_uv)).rgb, vec3(0.0));
    float k = u_mode.y;
    if (k > 0.0) c += k * (bloomTent(v_uv) * u_out.y - max(c - vec3(u_mode.z), vec3(0.0)));
    fragColor = vec4(tonemap(c, int(u_mode.x + 0.5), u_out.x), 1.0);
}
