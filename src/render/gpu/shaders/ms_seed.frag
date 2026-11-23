#version 450
// Copies the single-sample scene texture back into the multisampled scene target (sh::gpu::Targets) after something
// wrote the single-sample copy directly (a blit of the freeze frame, the clear colour of the picture rect). Both
// have the same size and storage rows, so the texel under each fragment is the one to copy; every sample of the
// pixel gets it.

#ifdef SH_RHI_VULKAN
#define SH_SAMPLER(slot) layout(set = 2, binding = slot)
#else
#define SH_SAMPLER(slot)
#endif

layout(location = 0) out vec4 fragColor;

SH_SAMPLER(0) uniform sampler2D s_src;

void main() {
    fragColor = texelFetch(s_src, ivec2(gl_FragCoord.xy), 0);
}
