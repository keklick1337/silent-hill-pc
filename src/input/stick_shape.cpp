// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "input/stick_shape.h"

#include <algorithm>
#include <cmath>

namespace sh::input {
namespace {

int to_axis(double v) {
    // The negative side reaches -32768, so a full push left or up is the extreme the original's pad gives.
    const double scaled = v < 0.0 ? v * 32768.0 : v * 32767.0;
    return static_cast<int>(std::clamp(std::lround(scaled), -32768L, 32767L));
}

}  // namespace

StickAxes shape_stick(int x, int y, const StickZones& zones) {
    const double fx = std::clamp(x / 32767.0, -1.0, 1.0);
    const double fy = std::clamp(y / 32767.0, -1.0, 1.0);
    const double radius = std::hypot(fx, fy);
    const double inner = std::clamp(zones.inner_percent, 0, 100) / 100.0;
    const double outer = 1.0 - std::clamp(zones.outer_percent, 0, 100) / 100.0;
    if (radius <= inner || radius == 0.0) {
        return {};
    }
    const double span = outer - inner;
    const double push = span <= 0.0 ? 1.0 : std::min(1.0, (radius - inner) / span);
    double ux = fx / radius;
    double uy = fy / radius;
    if (zones.square) {
        // A round gate gives about 0.71 on each axis at a full diagonal; the DualShock the game was tuned on reads
        // close to full there, and its tank controls read the axes one by one.
        const double longer = std::max(std::fabs(ux), std::fabs(uy));
        ux /= longer;
        uy /= longer;
    }
    return {to_axis(std::clamp(ux * push, -1.0, 1.0)), to_axis(std::clamp(uy * push, -1.0, 1.0))};
}

}  // namespace sh::input
