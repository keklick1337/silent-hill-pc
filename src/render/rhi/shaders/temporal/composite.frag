// SPDX-License-Identifier: GPL-3.0-or-later
// The result over its rect of the scene target (colour only; the target keeps its alpha).
#version 450
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

layout(set = 0, binding = 10) uniform sampler2D t_result;
layout(location = 0) out vec4 o_color;

void main() {
    o_color = vec4(texelFetch(t_result, ivec2(gl_FragCoord.xy - u_dst.xy), 0).rgb, 1.0);
}
