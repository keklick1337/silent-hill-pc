// SPDX-License-Identifier: GPL-3.0-or-later
// Motion of a posed mesh or a moving object where it is what the scene shows: its depth matches the scene's
// (tessellated models bulge a little off their flat triangles, hence the tolerance).
#version 450
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

layout(location = 0) in vec3 v_prev;
layout(location = 1) in vec3 v_cur;
layout(location = 2) flat in float v_kind;
layout(location = 0) out vec4 o_motion;  // xy: previous minus present picture coordinate, z: previous view depth, w: kind

void main() {
    float d = texelFetch(t_depth, ivec2(gl_FragCoord.xy + u_src.xy), 0).r;
    float sceneZ = d * u_jitter.z;
    float z = v_cur.z;
    float tol = max(24.0, 0.04 * z);
    bool translucent = v_kind > 2.5;
    if (d >= 1.0 || (translucent ? z > sceneZ + tol : abs(z - sceneZ) > tol)) discard;
    vec2 prevUv = UvOfNdc(v_prev.xy / max(v_prev.z, 1e-3));
    vec2 curUv = UvOfNdc(v_cur.xy / max(v_cur.z, 1e-3));
    o_motion = vec4(prevUv - curUv, v_prev.z, v_kind);
}
