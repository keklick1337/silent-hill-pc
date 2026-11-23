// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/geometry/hero_fill_light.h"

#include <algorithm>
#include <cmath>

#include "engine/config/pc_config.h"
#include "render/game_view.h"
#include "render/gpu/render_state.h"

namespace sh::render {

namespace {

constexpr int kLevels = 4;
constexpr float kStrengthOfLevel[kLevels] = {0.0f, 0.35f, 0.65f, 1.0f};

// From above and to the left of the camera: a light from straight ahead would flatten the model like a flash.
constexpr float kFromX = -0.35f;
constexpr float kFromY = -0.6f;
constexpr float kFromZ = -0.72f;

}  // namespace

FillLight hero_fill_light(int level, bool with_flashlight, bool flashlight_on) {
    const float length = std::sqrt(kFromX * kFromX + kFromY * kFromY + kFromZ * kFromZ);
    const bool yields = flashlight_on && !with_flashlight;
    return {{kFromX / length, kFromY / length, kFromZ / length},
            yields ? 0.0f : kStrengthOfLevel[std::clamp(level, 0, kLevels - 1)]};
}

void apply_hero_fill_light() {
    const FillLight light =
        hero_fill_light(g_PcConfig.heroLight, g_PcConfig.heroLightWithFlashlight != 0, flashlight_on());
    float* const out = flashlight().hero_light;
    std::copy(light.direction, light.direction + 3, out);
    out[3] = light.strength;
}

}  // namespace sh::render
