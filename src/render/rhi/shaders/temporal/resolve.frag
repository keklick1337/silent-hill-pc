// SPDX-License-Identifier: GPL-3.0-or-later
// resolve.comp as a fragment pass for tile-based GPUs (vk_temporal.cpp, `tiler`): the same arithmetic, drawn into the
// history as a colour attachment, which a phone keeps compressed where a storage image cannot be. The history's alpha
// holds the view depth that resolve.comp writes to its own R32F image (u_params.w: the motion buffer was drawn).
#version 450
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

layout(location = 0) out vec4 o_history;

// The largest finite 16-bit float: the far plane's 65535 would round to infinity.
const float kHalfMax = 65504.0;

ivec2 Clamped(ivec2 t) { return clamp(t, ivec2(0), ivec2(u_src.zw) - 1); }
vec3 Fetch(ivec2 t) { return max(texelFetch(t_color, Clamped(t) + ivec2(u_src.xy), 0).rgb, vec3(0.0)); }
float DepthAt(ivec2 t) { return texelFetch(t_depth, Clamped(t) + ivec2(u_src.xy), 0).r; }

vec3 ToYCoCg(vec3 c) {
    return vec3(dot(c, vec3(0.25, 0.5, 0.25)), dot(c, vec3(0.5, 0.0, -0.5)), dot(c, vec3(-0.25, 0.5, -0.25)));
}
vec3 FromYCoCg(vec3 y) { return vec3(y.x + y.y - y.z, y.x + y.z, y.x - y.y - y.z); }

// Blending in a range-compressed space keeps the beam's highlights above white from outweighing their neighbours.
vec3 Compress(vec3 c) { return c / (1.0 + max(max(c.r, c.g), c.b)); }
vec3 Expand(vec3 c) { return c / max(1.0 - max(max(c.r, c.g), c.b), 1e-4); }

// Catmull-Rom from five bilinear taps.
vec3 History(vec2 uv) {
    vec2 size = u_dst.zw;
    vec2 pos = uv * size;
    vec2 tc = floor(pos - 0.5) + 0.5;
    vec2 f = pos - tc;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 tc0 = (tc - 1.0) / size, tc3 = (tc + 2.0) / size, tc12 = (tc + w2 / w12) / size;
    float a = w12.x * w0.y, b = w0.x * w12.y, c = w12.x * w12.y, d = w3.x * w12.y, e = w12.x * w3.y;
    vec3 s = texture(t_history, vec2(tc12.x, tc0.y)).rgb * a + texture(t_history, vec2(tc0.x, tc12.y)).rgb * b +
             texture(t_history, tc12).rgb * c + texture(t_history, vec2(tc3.x, tc12.y)).rgb * d +
             texture(t_history, vec2(tc12.x, tc3.y)).rgb * e;
    return max(s / (a + b + c + d + e), vec3(0.0));
}

void main() {
    ivec2 o = ivec2(gl_FragCoord.xy);
    vec2 uv = (vec2(o) + 0.5) / u_dst.zw;
    vec2 rp = uv * u_src.zw;  // the pixel's centre in drawn pixels, unjittered
    // The sample of texel t lies at t + 0.5 - jitter.
    ivec2 ri = ivec2(floor(rp + u_jitter.xy));

    // The nearest surface around the pixel decides its motion, so edges of near things move with them.
    float bestD = 2.0;
    ivec2 bt = ri;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            ivec2 t = Clamped(ri + ivec2(dx, dy));
            float d = DepthAt(t);
            if (d < bestD) {
                bestD = d;
                bt = t;
            }
        }
    bool drawn = u_params.w > 0.5;
    vec4 m = drawn ? texelFetch(t_motion, bt, 0) : vec4(0.0);
    float kind = m.w;
    vec2 motion;
    float prevZ;
    if (kind > 0.5 && kind < 2.5) {
        motion = m.xy;
        prevZ = m.z;
    } else {
        vec2 bp = vec2(bt) + 0.5 - u_jitter.xy;
        vec3 v = ViewAt(bp, min(bestD, 1.0) * u_jitter.z);
        vec4 v1 = vec4(v, 1.0);
        vec3 pv = vec3(dot(u_reproj0, v1), dot(u_reproj1, v1), dot(u_reproj2, v1));
        vec2 pndc = u_prevProj.xy * pv.xy / max(pv.z, 1e-3) + u_prevProj.zw;
        motion = UvOfNdc(pndc) - bp / u_src.zw;
        prevZ = pv.z;
    }
    vec2 prevUv = uv + motion;

    if (u_jitter.w > 1.5) {
        vec2 px = motion * u_dst.zw;
        o_history = vec4(0.5 + px * 0.05, kind / 3.0, 0.0);
        return;
    }

    vec3 sum = vec3(0.0), m1 = vec3(0.0), m2 = vec3(0.0);
    vec3 lo = vec3(1e9), hi = vec3(-1e9);
    float wsum = 0.0, wmax = 0.0;
    bool reactive = kind > 2.5 || (drawn && texelFetch(t_motion, Clamped(ri), 0).w > 2.5);
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            ivec2 t = ri + ivec2(dx, dy);
            vec3 c = Compress(Fetch(t));
            vec2 d = vec2(t) + 0.5 - u_jitter.xy - rp;
            float w = exp(-2.29 * dot(d, d));
            sum += c * w;
            wsum += w;
            wmax = max(wmax, w);
            vec3 y = ToYCoCg(c);
            m1 += y;
            m2 += y * y;
            lo = min(lo, y);
            hi = max(hi, y);
        }
    vec3 cur = sum / max(wsum, 1e-5);

    bool valid = u_params.x < 0.5 && kind < 1.5 && prevZ > 0.0 && all(greaterThanEqual(prevUv, vec2(0.0))) &&
                 all(lessThanEqual(prevUv, vec2(1.0)));
    if (valid) {
        // Disoccluded: in the frame before, everything around that place was nearer than this point.
        vec4 hz = textureGather(t_history, prevUv, 3);
        float far = max(max(hz.x, hz.y), max(hz.z, hz.w));
        if (far > 0.0 && prevZ > far * 1.1 + 32.0) valid = false;
    }
    vec3 result = cur;
    if (valid) {
        m1 /= 9.0;
        vec3 sigma = sqrt(max(m2 / 9.0 - m1 * m1, vec3(0.0)));
        vec3 bmin = max(lo, m1 - sigma), bmax = min(hi, m1 + sigma);
        vec3 centre = 0.5 * (bmin + bmax), ext = max(0.5 * (bmax - bmin), vec3(1e-4));
        vec3 h = ToYCoCg(Compress(History(prevUv)));
        vec3 off = h - centre;
        vec3 r = abs(off / ext);
        float rmax = max(r.x, max(r.y, r.z));
        if (rmax > 1.0) h = centre + off / rmax;
        float alpha = 0.1 * mix(0.3, 1.0, wmax);
        alpha = clamp(alpha + length(motion * u_dst.zw) * 0.005, alpha, 0.25);
        if (reactive) alpha = max(alpha, 0.5);
        result = mix(FromYCoCg(h), cur, alpha);
    }
    o_history = vec4(Expand(max(result, vec3(0.0))), min(min(DepthAt(ri), 1.0) * u_jitter.z, kHalfMax));
}
