#version 450
// Full-target triangle of the feedback passes (src/render/gpu/feedback_pass.cpp). The fragment stages address their
// texels by gl_FragCoord, whose rows are storage rows on both backends, so no varying is needed.

layout(location = 0) in vec2 a_pos;

void main() {
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
