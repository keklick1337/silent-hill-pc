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
SH_SAMPLER(2) uniform sampler2D s_bloom2;
// Phones: the atmosphere's layer (vk_atmosphere.cpp, composite.frag kMode 1) and the scene depth, folded in here
// instead of a composite pass over the scene target (u_layer.x).
SH_SAMPLER(3) uniform sampler2D s_layer;
SH_SAMPLER(4) uniform sampler2D s_depth;
SH_SAMPLER(5) uniform sampler2D s_layerZ;
// Phones: the temporal pass's result over the whole scene target, read here instead of a composite pass (u_result.x).
SH_SAMPLER(6) uniform sampler2D s_result;

SH_UBO(0) uniform ResolveFs {
    vec4 u_mode;   // x: operator (0 clamp, 1 soft, 2 ACES, 3 Reinhard), y: bloom share, z: threshold, w: flip v
    vec4 u_out;    // x: white (largest value written), y: 1 / bloom levels, z: 1 = phone chain, w: weight of s_bloom2
    vec4 u_texel;  // xy: 1 / size of s_bloom2 (phone chain), zw: 1 / size of the first bloom level
    vec4 u_src;    // drawn into the window (phones, HdrPass::present): the source rect (texels, top row first)
    vec4 u_dst;    // and the present rect (pixels, top-left origin); w 0: a pass of the scene's size
    vec4 u_win;    // xy: the window's size (pixels)
    vec4 u_layer;  // x: 1 = fold the atmosphere's layer in, y: its step (pixels), z: view depth at window depth 1
    vec4 u_world;  // the world's viewport in the scene target (pixels): x, y, w, h
    vec4 u_result; // x: 1 = the scene is the temporal result (s_result), y: its sharpening
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

// The phone chain stops its upsampling one level short: the first level's own downsample and the upsampled rest
// (s_bloom2) are spread here, each by four bilinear taps on the diagonals whose spread matches the tents they stand
// for (sqrt(1/2) texel: one tent; sqrt(2/3): the tent below, the first level's grid and the tent above it).
vec3 box4(sampler2D s, vec2 uv, vec2 t) {
    return (texture(s, flipV(uv + vec2(-t.x, -t.y))).rgb + texture(s, flipV(uv + vec2(t.x, -t.y))).rgb +
            texture(s, flipV(uv + vec2(-t.x, t.y))).rgb + texture(s, flipV(uv + vec2(t.x, t.y))).rgb) *
           0.25;
}

vec3 bloomPhone(vec2 uv) {
    vec3 c = box4(s_bloom, uv, u_texel.zw * 0.70710678);
    if (u_out.w > 0.0) c += box4(s_bloom2, uv, u_texel.xy * 0.81649658) * u_out.w;
    return c;
}

// atmosphere/common.glsl's Noise and composite.frag's LayerAt: the same arithmetic, so the fold shows what the
// composite pass would have drawn.
float Noise(vec2 px) { return fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715)))); }

#ifndef SH_RHI_VULKAN
// GL 3.3 has no textureGather; the fold is the native Vulkan backend's only.
vec4 LayerAt(ivec2 t, float z) { return vec4(0.0, 0.0, 0.0, 1.0); }
#else
vec4 LayerAt(ivec2 t, float z) {
    int step = max(int(u_layer.y + 0.5), 1);
    vec2 f = vec2(t - ivec2(u_world.xy)) / float(step);
    ivec2 b0 = ivec2(floor(f));
    vec2 fr = f - vec2(b0);
    ivec2 size = textureSize(s_layer, 0);
    vec4 zs = textureGather(s_layerZ, (vec2(b0) + 1.0) / vec2(size), 0);
    float qz[4] = float[4](zs.w, zs.z, zs.x, zs.y);
    // All four at the pixel's depth (most of the picture): their bilinear mean, one filtered tap.
    if (z > 0.0) {
        float tol = 0.0005 * (0.05 * z + 8.0);
        vec4 gap = abs(vec4(qz[0], qz[1], qz[2], qz[3]) - z);
        if (all(lessThan(gap, vec4(tol))) && all(greaterThan(vec4(qz[0], qz[1], qz[2], qz[3]), vec4(0.0))))
            return textureLod(s_layer, (vec2(b0) + 0.5 + fr) / vec2(size), 0.0);
    }
    vec4 sum = vec4(0.0);
    float wsum = 0.0, bestGap = 1e30;
    int bestI = 0;
    for (int i = 0; i < 4; i++) {
        ivec2 o = ivec2(i & 1, i >> 1);
        float bw = (o.x == 1 ? fr.x : 1.0 - fr.x) * (o.y == 1 ? fr.y : 1.0 - fr.y) + 1e-3;
        float w = 0.0;
        if (z > 0.0 && qz[i] > 0.0)
            w = bw * clamp(1.0 - abs(qz[i] - z) / (0.05 * z + 8.0), 0.0, 1.0);
        else if (z <= 0.0 && qz[i] <= 0.0)
            w = bw;
        if (w > 0.0) sum += texelFetch(s_layer, clamp(b0 + o, ivec2(0), size - 1), 0) * w;
        wsum += w;
        float gap = z > 0.0 && qz[i] > 0.0 ? abs(qz[i] - z) : (z > 0.0) == (qz[i] > 0.0) ? 0.0 : 1e20;
        if (gap < bestGap) {
            bestGap = gap;
            bestI = i;
        }
    }
    if (wsum > 0.0) return sum / wsum;
    return texelFetch(s_layer, clamp(b0 + ivec2(bestI & 1, bestI >> 1), ivec2(0), size - 1), 0);
}
#endif

// temporal/composite_sharpen.frag: the result at pixel p, sharpened against its four neighbours.
vec3 resultAt(ivec2 p) {
    ivec2 last = textureSize(s_result, 0) - 1;
    vec3 c = texelFetch(s_result, clamp(p, ivec2(0), last), 0).rgb;
    float k = u_result.y;
    if (k > 0.0) {
        vec3 n = texelFetch(s_result, clamp(p + ivec2(0, -1), ivec2(0), last), 0).rgb;
        vec3 s = texelFetch(s_result, clamp(p + ivec2(0, 1), ivec2(0), last), 0).rgb;
        vec3 e = texelFetch(s_result, clamp(p + ivec2(1, 0), ivec2(0), last), 0).rgb;
        vec3 w = texelFetch(s_result, clamp(p + ivec2(-1, 0), ivec2(0), last), 0).rgb;
        vec3 lo = min(c, min(min(n, s), min(e, w))), hi = max(c, max(max(n, s), max(e, w)));
        c = clamp(c + 1.5 * k * (c - 0.25 * (n + s + e + w)), lo, hi);
    }
    return c;
}

// The scene colour at uv: the temporal result where it is given, then the atmosphere folded in where a layer is
// given (composite.frag kMode 2).
vec3 sceneAt(vec2 uv) {
    vec3 c;
    if (u_result.x > 0.5)
        c = max(resultAt(ivec2(floor(flipV(uv) * vec2(textureSize(s_scene, 0))))), vec3(0.0));
    else
        c = max(texture(s_scene, flipV(uv)).rgb, vec3(0.0));
    if (u_layer.x > 0.5) {
        ivec2 t = ivec2(floor(flipV(uv) * vec2(textureSize(s_scene, 0))));
        vec2 w0 = u_world.xy, w1 = u_world.xy + u_world.zw;
        if (all(greaterThanEqual(vec2(t), w0)) && all(lessThan(vec2(t), w1))) {
            float d = texelFetch(s_depth, t, 0).r;
            vec4 a = LayerAt(t, d >= 1.0 ? -1.0 : d * u_layer.z);
            float m = max(a.r, max(a.g, a.b));
            a.rgb += vec3((Noise(vec2(t) + 0.5) - 0.5) / 255.0 * clamp(m * 255.0, 0.0, 1.0));
            c = c * a.a + max(a.rgb, vec3(0.0));
        }
    }
    return c;
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
    vec2 uv = v_uv;
    // In the window, a pixel takes the source point present.frag maps its centre to.
    if (u_dst.w > 0.0) uv = flipV((u_src.xy + (WindowPixel(u_win.xy) - u_dst.xy) / u_dst.zw * u_src.zw) /
                                  vec2(textureSize(s_scene, 0)));
    vec3 c = sceneAt(uv);
    float k = u_mode.y;
    if (k > 0.0) {
        vec3 b = u_out.z > 0.5 ? bloomPhone(uv) : bloomTent(uv);
        c += k * (b * u_out.y - max(c - vec3(u_mode.z), vec3(0.0)));
    }
    fragColor = vec4(tonemap(c, int(u_mode.x + 0.5), u_out.x), 1.0);
}
