// Frame-wide shading inputs the game sets and the renderer reads when it draws: the world fog ramp.
#pragma once

#include <cstdint>

namespace sh::render
{

// The world fog ramp of the frame: 128 ramp bytes, the bucket depth shift, the intensity (Q12), whether fog is
// on, and the far cull distance (view Z; 0 or INT32_MAX = none, negative = keep the last one).
void set_world_fog(const unsigned char* ramp, int depth_shift, int intensity, bool enabled, int far_distance);

} // namespace sh::render

namespace sh::shaderstate
{

// World fog ramp as the shaders' WorldFog block takes it; version() moves whenever the ramp or its parameters do.
class FogRamp
{
public:
    void set(const unsigned char* ramp, int depthShift, int intensity, bool enabled, int farDist);
    const float* ramp() const { return ramp_; }     // 128 floats = 32 vec4
    const float* params() const { return params_; } // 4 floats
    unsigned version() const { return version_; }

private:
    float ramp_[128]{};
    float params_[4]{};
    unsigned version_ = 1;
};

FogRamp& fogRamp();

} // namespace sh::shaderstate
