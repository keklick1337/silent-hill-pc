// SPDX-License-Identifier: GPL-3.0-or-later
// Motion triangles of posed meshes and moving objects (vk_temporal.cpp): positions are projected on the CPU.
#version 450

layout(location = 0) in vec4 a_clip;
layout(location = 1) in vec4 a_prev;  // xyz: previous unjittered clip x, y, w; w: kind
layout(location = 2) in vec4 a_cur;   // xyz: present unjittered clip x, y, w

layout(location = 0) out vec3 v_prev;
layout(location = 1) out vec3 v_cur;
layout(location = 2) flat out float v_kind;

void main() {
    v_prev = a_prev.xyz;
    v_cur = a_cur.xyz;
    v_kind = a_prev.w;
    gl_Position = a_clip;
}
