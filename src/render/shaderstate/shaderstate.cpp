#include "render/shaderstate/shaderstate.h"

#include <cstring>

namespace sh::shaderstate
{

void FogRamp::set(const unsigned char* ramp, int depthShift, int intensity, bool enabled, int farDist)
{
    float p[4] = { enabled ? 1.0f : 0.0f, 128.0f / static_cast<float>(1u << (depthShift & 31)), static_cast<float>(intensity),
                   farDist > 0 && farDist < 0x7FFFFFFF ? static_cast<float>(farDist) : 0.0f };
    if (farDist < 0)
        p[3] = params_[3];
    bool changed = std::memcmp(p, params_, sizeof p) != 0;
    for (int i = 0; i < 128 && !changed; i++)
        changed = ramp_[i] != static_cast<float>(ramp[i]);
    if (!changed)
        return;
    std::memcpy(params_, p, sizeof p);
    for (int i = 0; i < 128; i++)
        ramp_[i] = static_cast<float>(ramp[i]);
    version_++;
}

FogRamp& fogRamp() { static FogRamp s; return s; }

} // namespace sh::shaderstate

namespace sh::render
{

void set_world_fog(const unsigned char* ramp, int depth_shift, int intensity, bool enabled, int far_distance)
{
    shaderstate::fogRamp().set(ramp, depth_shift, intensity, enabled, far_distance);
}

} // namespace sh::render
