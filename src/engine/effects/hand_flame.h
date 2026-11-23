// SPDX-License-Identifier: GPL-3.0-or-later
// The flame of the match in Harry's right hand: a flickering textured
// quad at a jittered point of the hand bone, its size the same on screen at any depth, its brightness flickering
// down now and then.
//
// The eight random words drawn per frame,
// their uses and the flicker counter are the original's. The host projects through the hand bone (and with no
// transform for the quad's corners) and places the quad.

#pragma once

#include <array>
#include <cstdint>

#include "engine/effects/effect_prims.h"
#include "render/draw3d/draw3d.h"

namespace sh::effects {

class HandFlameHost {
public:
    virtual ~HandFlameHost() = default;
    virtual std::int32_t rand16() = 0;
    virtual std::int32_t delta_time() = 0;
    // A point in the right hand bone's space: screen point and the depth word (screen z / 4).
    virtual ScreenPoint project_hand(const Vec3s& local) = 0;
    // A point projected with no rotation and no translation.
    virtual ScreenPoint project_flat(const Vec3s& v) = 0;
    virtual void emit(const draw3d::Polygon& flame, std::int32_t bucket) = 0;
};

class HandFlame {
public:
    void draw(HandFlameHost& host);

private:
    std::uint32_t flicker_ = 0;
    std::array<std::int16_t, 8> noise_{};
};

}  // namespace sh::effects
