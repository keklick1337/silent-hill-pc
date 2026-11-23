// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The running game's element of map7_s03's boss light beam steps, as the map tables fill it (map_table_types.cpp):
// the step's time and two beam ends, each between two anchor points of the same map. The final boss's effects read
// it with their own record of the same layout.

#pragma once

#include <cstdint>

namespace sh::maps {

struct BossBeamEnd {
    const void* from;
    std::int32_t from_param;
    const void* to;
    std::int32_t to_param;
    std::int16_t motion[8];
};

struct BossBeamStep {
    std::int32_t time;
    BossBeamEnd ends[2];
};

}  // namespace sh::maps
