// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/maps/map_globals.h"
#include "engine/screens/staff_roll_clock.h"

namespace sh::screens {

std::int32_t& staff_roll_clock() {
    static std::int32_t clock = 0;
    return clock;
}

}  // namespace sh::screens

namespace {

// The ending scenes add the vblanks to the staff roll clock by name (map_vars "credits_vblank_clock").
[[maybe_unused]] const bool g_map_globals_bound = [] {
    sh::maps::map_globals().bind("credits_vblank_clock", &sh::screens::staff_roll_clock());
    return true;
}();

}  // namespace
