#version 450
// Flashlight shadow casters (shadow_pass.cpp): the view-space position of every casting triangle through the light
// matrix. Eligibility (validated view position, not suppressed, finite, beyond the near plane) is decided on the CPU,
// so the stream holds only casters. One source for both RHI backends, like render/rhi/shaders/prim_batch.vert.

#ifdef SH_RHI_VULKAN
#define SH_UBO(slot) layout(std140, set = 1, binding = slot)
#else
#define SH_UBO(slot) layout(std140)
#endif

layout(location = 0) in vec3 a_viewpos;

SH_UBO(0) uniform ShadowFrame {
    mat4 u_lightMatrix;  // view space -> light clip (GL convention, Z in [-1, 1])
};

void main() {
    gl_Position = u_lightMatrix * vec4(a_viewpos, 1.0);
#ifdef SH_RHI_CLIP_Z01
    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
#endif
}
