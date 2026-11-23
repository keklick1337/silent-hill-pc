// SPDX-License-Identifier: GPL-3.0-or-later
// The temporal passes' shared block and helpers (vk_temporal.cpp).

layout(std140, set = 0, binding = 0) uniform Temporal {
    vec4 u_src;       // xy: origin of the drawn picture in the target (pixels), zw: its size
    vec4 u_dst;       // xy: origin of the result, zw: its size
    vec4 u_proj;      // view to NDC without the jitter: ndc = xy * v.xy / v.z + zw
    vec4 u_prevProj;  // the same of the frame before
    vec4 u_jitter;    // xy: the scene's shift in src pixels (y down), z: view depth at window depth 1, w: mode
    vec4 u_reproj0;   // rows of the 3 x 4 matrix to the previous view space of a still point
    vec4 u_reproj1;
    vec4 u_reproj2;
    vec4 u_params;    // x: history reset, y: sharpness, z: first frame of a history, w: -
};

layout(set = 0, binding = 1) uniform sampler2D t_color;
layout(set = 0, binding = 2) uniform sampler2D t_depth;
layout(set = 0, binding = 3) uniform sampler2D t_motion;
layout(set = 0, binding = 4) uniform sampler2D t_history;
layout(set = 0, binding = 5) uniform sampler2D t_historyDepth;

// Unjittered NDC of a point of the src picture (pixels from its top-left corner) to its view position at depth z.
vec3 ViewAt(vec2 p, float z) {
    vec2 ndc = vec2(2.0 * p.x / u_src.z - 1.0, 1.0 - 2.0 * p.y / u_src.w);
    return vec3((ndc - u_proj.zw) / u_proj.xy * z, z);
}

// Normalised picture coordinate (0..1, top-left origin) of NDC.
vec2 UvOfNdc(vec2 ndc) { return vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5); }

float Luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
