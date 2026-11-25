// SPDX-License-Identifier: GPL-3.0-or-later
// spatial.comp as a fragment pass for tile-based GPUs (vk_temporal.cpp, `tiler`), drawn into the history.
#version 450
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

layout(location = 0) out vec4 o_history;

const float kPi = 3.14159265;

float Lanczos2(float x) {
    x = abs(x);
    if (x < 1e-4) return 1.0;
    if (x >= 2.0) return 0.0;
    return 2.0 * sin(kPi * x) * sin(kPi * x * 0.5) / (kPi * kPi * x * x);
}

vec3 Fetch(ivec2 t) {
    t = clamp(t, ivec2(0), ivec2(u_src.zw) - 1);
    return max(texelFetch(t_color, t + ivec2(u_src.xy), 0).rgb, vec3(0.0));
}

void main() {
    ivec2 o = ivec2(gl_FragCoord.xy);
    vec2 rp = (vec2(o) + 0.5) / u_dst.zw * u_src.zw;
    ivec2 base = ivec2(floor(rp - 0.5));
    vec3 sum = vec3(0.0);
    float wsum = 0.0;
    vec3 lo = vec3(1e9), hi = vec3(-1e9);
    for (int j = -1; j <= 2; ++j)
        for (int i = -1; i <= 2; ++i) {
            ivec2 t = base + ivec2(i, j);
            vec3 c = Fetch(t);
            vec2 d = rp - (vec2(t) + 0.5);
            float w = Lanczos2(d.x) * Lanczos2(d.y);
            sum += c * w;
            wsum += w;
            if (i >= 0 && i <= 1 && j >= 0 && j <= 1) {
                lo = min(lo, c);
                hi = max(hi, c);
            }
        }
    o_history = vec4(clamp(sum / max(wsum, 1e-5), lo, hi), 1.0);
}
