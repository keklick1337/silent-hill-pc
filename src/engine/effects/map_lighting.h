// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The map light machine: the session's environment mix
// (sh::game::Session::system.environment) blends two pairs of environment presets (flashlight off and on) towards a
// target pair, by a watched value or at once, and the flashlight fades between the two halves; every frame the
// blended state goes to the drawn atmosphere (sh::world::atmosphere()) with the point light on Harry's light rig.
// The presets are gamedata/config/map_info.json "environments", the pairs per light mode and the rigs
// gamedata/effects/map_lighting.json. Implemented in src/engine/effects/game/map_lighting.cpp (it drives the screen
// tint and the light state set-up of the drawers).

#include <cstdint>

#include "engine/game_state/session.h"

namespace sh::effects {

// The width of the value a transition watches.
enum class WatchedValue : std::int32_t { None = 0, S8 = 1, U8 = 2, S16 = 3, U16 = 4, S32 = 5 };

class MapLighting {
public:
    explicit MapLighting(game::EnvironmentMix& mix) : mix_(mix) {}

    // The start pair of the map type and light mode now running.
    void assign_for_map();
    // Target presets `to_off`/`to_on`, reached as `value` (of width `type`) goes from `from` to `to`; at once with
    // WatchedValue::None (on the next update).
    void transition(std::int32_t to_off, std::int32_t to_on, WatchedValue type, const void* value, std::int32_t from,
                    std::int32_t to);
    // transition(..., None) followed by an update.
    void set_now(std::int32_t to_off, std::int32_t to_on);
    // One frame: the flashlight fade, the transition, the blend, the flash, the drawn atmosphere and the hand flame.
    void update();

    void set_flashlight(bool on);
    void toggle_flashlight();
    bool flashlight_on() const { return mix_.flashlight_on != 0; }
    // A flash of light on the next update (gunfire).
    void flash() { mix_.flash_effect = 1; }

    // Harry's light rigs.
    void use_flashlight_rig();
    void use_load_screen_rig();

    // The snow and rain colours of the half the flashlight selects.
    game::Rgbx snow_color() const;
    game::Rgbx rain_color() const;

private:
    std::uint32_t watched_value() const;
    void apply_to_atmosphere(const game::EnvironmentState& state);

    game::EnvironmentMix& mix_;
};

// The session's machine.
inline MapLighting map_lighting() { return MapLighting(game::session().system.environment); }

}  // namespace sh::effects
