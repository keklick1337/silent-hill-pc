#pragma once
// Texel terms of a replacement (override) texture: what the textured shaders need to sample a pool slot that
// is registered at its native size but may be stored at a higher resolution.
namespace sh::render {

struct OverrideTexTerms {
    float texelSize[2];  // 1 / native size per axis (divisor clamped to >= 1 so a half-registered slot stays finite)
    float offset[2];     // sub-rect origin in native texels
    float halfHires[2];  // half a hires texel in native-texel units, capped at 0.5; 0 = no clamp (hires size unknown)
};

OverrideTexTerms ComputeOverrideTexTerms(int width, int height, int offsetX, int offsetY, int hiresW, int hiresH);

}  // namespace sh::render
