#version 450
#extension GL_GOOGLE_include_directive : require
// Over the scene colour with blend src + dst * src.a (RENDER.md 7.10): the occlusion k darkens the surface but not
// the fog in front of it, then the scattered beam is added. With neither effect src is (0, 1): the image is unchanged.
//
// A phone may leave the composite to the frame's resolve (kMode, vk_atmosphere.cpp): mode 1 writes the layer, the
// same terms at each occlusion estimate's pixel, (scattered light, k) and the view z there (negative: no depth),
// into two images of the estimates' grid; mode 2 draws that layer over the scene colour as mode 0 would, each pixel
// taking the four nearest layer texels by place and depth (LayerAt, the same arithmetic as hdr_resolve.frag's).
#include "common.glsl"
layout(set = 0, binding = 3, r32f) uniform readonly image2D i_ao;
layout(set = 0, binding = 6) uniform sampler3D s_volume;
layout(set = 0, binding = 8) uniform sampler2D s_layer;
layout(set = 0, binding = 9) uniform sampler2D s_layerZ;
layout(constant_id = 2) const int kMode = 0;
layout(location = 0) out vec4 fragColor;
layout(location = 1) out float fragZ;  // mode 1 only

// (scattered light, k) of target pixel t at view depth z; `noise`: the anti-banding dither on the beam.
vec4 Atmos(ivec2 t, float z, bool noise) {
    vec2 px = vec2(t) + 0.5;
    float k = 1.0;
    if (u_grid.w > 0.5 && z > 0.0) {
        // A depth-aware 3x3 mean hides the taps' rotation pattern. At a step of 2 the taps are the estimates
        // around the pixel, weighed by the depth of the pixel each was made at: the mean is also the upsampling.
        int step = max(int(u_grid.w + 0.5), 1);
        ivec2 at = t - ivec2(u_view.xy);
        ivec2 last = (ivec2(u_view.zw) + step - 1) / step - 1;
        ivec2 centre = at / step;
        float sum = 0.0, wsum = 0.0;
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                ivec2 q = clamp(centre + ivec2(x, y), ivec2(0), last);
                float qa, qz;
                if (kPhone) {
                    vec2 e = UnpackAo(imageLoad(i_ao, q).r);
                    qa = e.x;
                    qz = e.y;
                } else {
                    qz = ViewZ(min(q * step, ivec2(u_view.zw) - 1) + ivec2(u_view.xy));
                    qa = imageLoad(i_ao, q).r;
                }
                float w = qz > 0.0 ? clamp(1.0 - abs(qz - z) / (0.05 * z + 8.0), 0.0, 1.0) : 0.0;
                sum += qa * w;
                wsum += w;
            }
        }
        float ao = wsum > 0.0 ? sum / wsum : 1.0;
        // Only the map's own light is occluded: where the beam lights the surface the occlusion fades out.
        vec3 p = ViewPos(px, z);
        float lit = clamp(Beam(p) * BeamShadow(p) * 4.0, 0.0, 1.0);
        k = 1.0 - u_ao.x * (1.0 - ao) * (1.0 - lit);
    }
    // Without occlusion the term is zero; a phone skips the ramp for it.
    vec3 add = z > 0.0 && (!kPhone || k < 1.0) ? u_fog.rgb * (1.0 - Transmittance(z)) * (1.0 - k) : vec3(0.0);
    if (u_ao.w > 0.5) {
        float s = SliceOf(z > 0.0 ? z : u_depth.z);
        vec3 uvw = vec3((px - u_view.xy) / u_view.zw, (s + 0.5) / u_grid.z);
        vec3 beam = texture(s_volume, uvw).rgb;
        if (noise) {
            // Half an 8-bit step of noise where there is light, against banding in the smooth beam.
            float m = max(beam.r, max(beam.g, beam.b));
            beam += vec3((Noise(px) - 0.5) / 255.0 * clamp(m * 255.0, 0.0, 1.0));
        }
        add += max(beam, vec3(0.0));
    }
    return vec4(add, k);
}

// The layer's grid: estimate q stands for target pixel q * step + the viewport's corner (ao.comp).
int LayerStep() { return max(int(u_lightInfo.y + 0.5), 1); }

// (scattered light, k) at target pixel t of view depth z (negative: no depth) from the four layer texels around it,
// each by its distance and how near its depth is; none near: the nearest in depth.
vec4 LayerAt(ivec2 t, float z) {
    int step = LayerStep();
    vec2 f = vec2(t - ivec2(u_view.xy)) / float(step);
    ivec2 b0 = ivec2(floor(f));
    vec2 fr = f - vec2(b0);
    ivec2 size = textureSize(s_layer, 0);
    // The four depths in one gather at the corner between them: (b0.x, b0.y + 1), (+1, +1), (+1, b0.y), b0.
    vec4 zs = textureGather(s_layerZ, (vec2(b0) + 1.0) / vec2(size), 0);
    float qz[4] = float[4](zs.w, zs.z, zs.x, zs.y);
    // All four at the pixel's depth (most of the picture): their bilinear mean, one filtered tap.
    if (z > 0.0) {
        float tol = 0.0005 * (0.05 * z + 8.0);
        vec4 gap = abs(vec4(qz[0], qz[1], qz[2], qz[3]) - z);
        if (all(lessThan(gap, vec4(tol))) && all(greaterThan(vec4(qz[0], qz[1], qz[2], qz[3]), vec4(0.0))))
            return textureLod(s_layer, (vec2(b0) + 0.5 + fr) / vec2(size), 0.0);
    }
    vec4 sum = vec4(0.0);
    float wsum = 0.0, bestGap = 1e30;
    int bestI = 0;
    for (int i = 0; i < 4; i++) {
        ivec2 o = ivec2(i & 1, i >> 1);
        float bw = (o.x == 1 ? fr.x : 1.0 - fr.x) * (o.y == 1 ? fr.y : 1.0 - fr.y) + 1e-3;
        float w = 0.0;
        if (z > 0.0 && qz[i] > 0.0)
            w = bw * clamp(1.0 - abs(qz[i] - z) / (0.05 * z + 8.0), 0.0, 1.0);
        else if (z <= 0.0 && qz[i] <= 0.0)
            w = bw;
        if (w > 0.0) sum += texelFetch(s_layer, clamp(b0 + o, ivec2(0), size - 1), 0) * w;
        wsum += w;
        float gap = z > 0.0 && qz[i] > 0.0 ? abs(qz[i] - z) : (z > 0.0) == (qz[i] > 0.0) ? 0.0 : 1e20;
        if (gap < bestGap) {
            bestGap = gap;
            bestI = i;
        }
    }
    if (wsum > 0.0) return sum / wsum;
    return texelFetch(s_layer, clamp(b0 + ivec2(bestI & 1, bestI >> 1), ivec2(0), size - 1), 0);
}

void main() {
    if (kMode == 1) {
        // One texel of the layer: the terms at its estimate's pixel, without the dither (the resolve adds it).
        ivec2 q = ivec2(gl_FragCoord.xy);
        ivec2 t = min(q * LayerStep(), ivec2(u_view.zw) - 1) + ivec2(u_view.xy);
        float z = ViewZ(t);
        fragColor = Atmos(t, z, false);
        fragZ = z > 0.0 ? z : -1.0;
        return;
    }
    ivec2 t = ivec2(gl_FragCoord.xy);
    float z = ViewZ(t);
    if (kMode == 2) {
        vec4 a = LayerAt(t, z);
        float m = max(a.r, max(a.g, a.b));
        a.rgb += vec3((Noise(vec2(t) + 0.5) - 0.5) / 255.0 * clamp(m * 255.0, 0.0, 1.0));
        fragColor = vec4(max(a.rgb, vec3(0.0)), a.a);
        return;
    }
    fragColor = Atmos(t, z, true);
}
