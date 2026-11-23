// SPDX-License-Identifier: GPL-3.0-or-later
//
// The fill light on the player's character (config keys hero_light and hero_light_flashlight): a soft directional
// light the character shader adds, so the model does not go flat black where the scene gives it no light.
#pragma once

namespace sh::render {

struct FillLight {
    float direction[3];  // unit vector in view space (+y down, -z towards the camera)
    float strength;      // 0 = off
};

// The light of a hero_light level (0..3, clamped). With `with_flashlight` false the light gives way to the
// flashlight while that is on.
FillLight hero_fill_light(int level, bool with_flashlight, bool flashlight_on);

// Hands the light of the current settings to the scene shaders.
void apply_hero_fill_light();

}  // namespace sh::render
