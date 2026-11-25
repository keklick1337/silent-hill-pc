// SPDX-License-Identifier: GPL-3.0-or-later
// sharpen.comp and composite.frag in one pass for tile-based GPUs (vk_temporal.cpp, `tiler`): the history is
// sharpened as it is drawn over the scene target, so no result image is written and read back.
#version 450
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

layout(set = 0, binding = 9) uniform sampler2D t_stage;
layout(location = 0) out vec4 o_color;

vec3 At(ivec2 p) { return texelFetch(t_stage, clamp(p, ivec2(0), ivec2(u_dst.zw) - 1), 0).rgb; }

void main() {
    ivec2 o = ivec2(gl_FragCoord.xy - u_dst.xy);
    vec3 c = At(o);
    float k = u_params.y;
    if (k > 0.0) {
        vec3 n = At(o + ivec2(0, -1)), s = At(o + ivec2(0, 1)), e = At(o + ivec2(1, 0)), w = At(o + ivec2(-1, 0));
        vec3 lo = min(c, min(min(n, s), min(e, w))), hi = max(c, max(max(n, s), max(e, w)));
        c = clamp(c + 1.5 * k * (c - 0.25 * (n + s + e + w)), lo, hi);
    }
    o_color = vec4(c, 1.0);
}
