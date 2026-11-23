#version 450
#extension GL_GOOGLE_include_directive : require
// Over the scene colour with blend src + dst * src.a (RENDER.md 7.10): the occlusion k darkens the surface but not
// the fog in front of it, then the scattered beam is added. With neither effect src is (0, 1): the image is unchanged.
#include "common.glsl"
layout(set = 0, binding = 3, r32f) uniform readonly image2D i_ao;
layout(set = 0, binding = 6) uniform sampler3D s_volume;
layout(location = 0) out vec4 fragColor;

void main() {
    ivec2 t = ivec2(gl_FragCoord.xy);
    vec2 px = vec2(t) + 0.5;
    float z = ViewZ(t);
    float k = 1.0;
    if (u_grid.w > 0.5 && z > 0.0) {
        // A depth-aware 3x3 mean hides the taps' rotation pattern.
        ivec2 at = t - ivec2(u_view.xy);
        ivec2 last = ivec2(u_view.zw) - 1;
        float sum = 0.0, wsum = 0.0;
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                ivec2 q = clamp(at + ivec2(x, y), ivec2(0), last);
                float qz = ViewZ(q + ivec2(u_view.xy));
                float w = qz > 0.0 ? clamp(1.0 - abs(qz - z) / (0.05 * z + 8.0), 0.0, 1.0) : 0.0;
                sum += imageLoad(i_ao, q).r * w;
                wsum += w;
            }
        }
        float ao = wsum > 0.0 ? sum / wsum : 1.0;
        // Only the map's own light is occluded: where the beam lights the surface the occlusion fades out.
        vec3 p = ViewPos(px, z);
        float lit = clamp(Beam(p) * BeamShadow(p) * 4.0, 0.0, 1.0);
        k = 1.0 - u_ao.x * (1.0 - ao) * (1.0 - lit);
    }
    vec3 add = z > 0.0 ? u_fog.rgb * (1.0 - Transmittance(z)) * (1.0 - k) : vec3(0.0);
    if (u_ao.w > 0.5) {
        float s = SliceOf(z > 0.0 ? z : u_depth.z);
        vec3 uvw = vec3((px - u_view.xy) / u_view.zw, (s + 0.5) / u_grid.z);
        vec3 beam = texture(s_volume, uvw).rgb;
        // Half an 8-bit step of noise where there is light, against banding in the smooth beam.
        float m = max(beam.r, max(beam.g, beam.b));
        beam += vec3((Noise(px) - 0.5) / 255.0 * clamp(m * 255.0, 0.0, 1.0));
        add += max(beam, vec3(0.0));
    }
    fragColor = vec4(add, k);
}
