// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/devtools/position_snippets.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace sh::devtools {

std::int32_t q12_of_metres(float metres) {
    // volatile: the product must be rounded to float before the truncation, as on the console and in the readers.
    volatile float scaled = metres * 4096.0f;
    return static_cast<std::int32_t>(scaled);
}

std::string metres_text(std::int32_t q12) {
    const double exact = static_cast<double>(q12) / 4096.0;
    char buf[48];
    for (int decimals = 1; decimals <= 6; ++decimals) {
        std::snprintf(buf, sizeof(buf), "%.*f", decimals, exact);
        if (q12_of_metres(std::strtof(buf, nullptr)) == q12) return buf;
    }
    // Truncation toward zero of a value just past the step: nudge away from zero by half a step of the last digit.
    for (int decimals = 4; decimals <= 9; ++decimals) {
        const double nudge = (q12 < 0 ? -0.5 : 0.5) * std::pow(10.0, -decimals);
        std::snprintf(buf, sizeof(buf), "%.*f", decimals, exact + nudge);
        if (q12_of_metres(std::strtof(buf, nullptr)) == q12) return buf;
    }
    std::snprintf(buf, sizeof(buf), "%.6f", exact);
    return buf;
}

Q12Point q12_from_geometry(double x, double y, double z) {
    // 256 geometry units per metre, 4096 Q12 steps: 16 Q12 per unit.
    return {static_cast<std::int32_t>(std::lround(x * 16.0)), static_cast<std::int32_t>(std::lround(y * 16.0)),
            static_cast<std::int32_t>(std::lround(z * 16.0))};
}

std::vector<Snippet> position_snippets(const Q12Point& p, std::int32_t yaw) {
    const std::string mx = metres_text(p.x), my = metres_text(p.y), mz = metres_text(p.z);
    char buf[256];
    std::vector<Snippet> out;
    std::snprintf(buf, sizeof(buf), "sh.player_set_pos(%d, %d, %d) -- %s, %s, %s m", p.x, p.y, p.z, mx.c_str(),
                  my.c_str(), mz.c_str());
    out.push_back({"player position (Lua, Q12)", buf});
    std::snprintf(buf, sizeof(buf), "sh.vec.set(0, %d, %d, %d) -- %s, %s, %s m", p.x, p.y, p.z, mx.c_str(), my.c_str(),
                  mz.c_str());
    out.push_back({"scratch vector vec:0 (Lua, Q12)", buf});
    std::snprintf(buf, sizeof(buf), "sh.chara_spawn(\"GreyChild\", 0, %d, %d, %d, 3) -- (%s, %s)", p.x, p.z, yaw,
                  mx.c_str(), mz.c_str());
    out.push_back({"spawn on the ground here (Lua, x z Q12)", buf});
    std::snprintf(buf, sizeof(buf), "{ x = %d, y = %d, z = %d } -- %s, %s, %s m", p.x, p.y, p.z, mx.c_str(),
                  my.c_str(), mz.c_str());
    out.push_back({"position table (Lua, Q12)", buf});
    std::snprintf(buf, sizeof(buf), "\"position\": [%s, %s, %s]", mx.c_str(), my.c_str(), mz.c_str());
    out.push_back({"world_objects.json position (metres)", buf});
    std::snprintf(buf, sizeof(buf), "sh.camera_pos(%s, %s, %s, 0, 0, 0, 0, 0, 0, 0, true)", mx.c_str(), my.c_str(),
                  mz.c_str());
    out.push_back({"event camera here (Lua, metres)", buf});
    return out;
}

}  // namespace sh::devtools
