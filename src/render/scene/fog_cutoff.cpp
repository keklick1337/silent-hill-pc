// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/scene/fog_cutoff.h"

#include <algorithm>
#include <cmath>

namespace sh::scene {

namespace {

// Fog this close to full leaves less than half a step of an 8-bit colour of the surface: the picture is the same.
constexpr float kInvisible = 1.0f / 512.0f;

// Whether a ramp byte gives full fog at this strength, as the shaders compute it: FogAmountAtZ (soft) or the vertex
// byte of FogByteAtZ (classic), times u_fog.a, the soft fog's knee above 0.85.
bool full_at(float byte, float intensity, float strength, bool soft_fog) {
    const float fa = std::clamp(byte * 16.0f + intensity, 0.0f, 4096.0f);
    const float amount = soft_fog ? fa / 4096.0f : std::floor((fa * 127.0f + 2048.0f) / 4096.0f) / 127.0f;
    const float linear = amount * strength;
    float fog = std::clamp(linear, 0.0f, 1.0f);
    if (soft_fog && linear > 0.85f) {
        const float k = std::clamp((linear - 0.85f) / 0.15f, 0.0f, 1.0f);
        fog = 0.85f + 0.15f * (k - k * k + k * k * k);
    }
    return fog >= 1.0f - kInvisible;
}

}  // namespace

float fog_full_depth(const rhi::FogRampUniforms& fog, float strength, bool soft_fog) {
    const auto& p = fog.params;
    if (p[0] < 0.5f || p[1] <= 0.0f) return kNoFogCutoff;
    float depth = kNoFogCutoff;
    // Past the ramp's last entry the shaders take 255; from the first entry after which every one is full, the fog
    // stays full (the soft fog interpolates between two full entries there).
    if (strength > 0.0f && full_at(255.0f, p[2], strength, soft_fog)) {
        int first = static_cast<int>(fog.ramp.size());
        while (first > 0 && full_at(fog.ramp[static_cast<std::size_t>(first - 1)], p[2], strength, soft_fog)) --first;
        depth = static_cast<float>(first) / p[1];
    }
    // soft_fog P4: full fog at the world's cull distance whatever the strength.
    if (soft_fog && p[3] > 0.0f) depth = std::min(depth, p[3]);
    return depth;
}

float fog_draw_cutoff(const rhi::FogRampUniforms& fog, float strength, bool soft_fog) {
    const float depth = fog_full_depth(fog, strength, soft_fog);
    return depth >= kNoFogCutoff ? kNoFogCutoff : depth * (1.0f + kFogCutoffMargin);
}

}  // namespace sh::scene
