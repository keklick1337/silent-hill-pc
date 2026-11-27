// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Lights a script gives a character to carry (sh.light.carry): a spot or a point light that follows one bone of the
// character in a slot, the way the flashlight follows the player's. A record of what was asked for and nothing more:
// the frame's light host resolves the bone every frame and hands the renderer a light in world units, so the
// simulation never reads them. They last until the script drops them or the map changes.
#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace sh::world {

struct CarriedLight {
    std::string owner;  // mod id ("" for the base game's scripts)
    std::string id;     // unique per owner
    int slot = 0;       // the character's slot: 0 the player, 1.. the others
    // The bone the light sits on: an index of the character's skeleton, or (index < 0) a name of the player's
    // skeleton ("Torso"), which the host resolves.
    int bone = -1;
    std::string bone_name;
    std::array<float, 3> position{0, 0, 0};   // metres in the bone's space (y down)
    std::array<float, 3> direction{0, 0, 0};  // the beam's axis in the bone's space; zero: a point light
    float cone_inner = 16.0f;                 // degrees from the axis: full light inside, none past outer
    float cone_outer = 28.0f;
    std::array<float, 3> color{1, 1, 1};  // linear
    float intensity = 1.0f;
    float range = 8.0f;    // metres
    float radius = 0.03f;  // metres: the emitter's size (shadow softness)
    bool casts_shadows = true;
    bool gi = true;
};

// Gives (or replaces) the light `id` of `owner`.
void carry_light(CarriedLight light);
// Takes away one light of `owner` (or all of them with an empty id); true when one went.
bool drop_carried_lights(std::string_view owner, std::string_view id);
// A map change: no character of the map left carries anything.
void clear_carried_lights();
const std::vector<CarriedLight>& carried_lights();

}  // namespace sh::world
