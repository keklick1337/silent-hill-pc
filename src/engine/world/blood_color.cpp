// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/world/blood_color.h"

namespace sh::world {

namespace {

// Slot of an extra colour setting that applies to every kind, or -1.
std::int32_t global_override(std::int32_t extra) {
    switch (extra) {
        case 3: return 6;
        case 6: return 7;
        case 9: return 5;
        case 12: return 4;
        case 13: return 0;
        default: return -1;
    }
}

// Slot for the settings that apply to the ordinary kinds (groups of 1-2, 4-5, 7-8, 10-11).
std::int32_t group_slot(std::int32_t extra) {
    switch (extra) {
        case 1: case 2: return 6;
        case 4: case 5: return 7;
        case 7: case 8: return 5;
        case 10: case 11: return 4;
        default: return 0;
    }
}

}  // namespace

std::int32_t blood_color_slot(std::int32_t kind, std::int32_t extra_color) {
    if (kind == 15) return 0;
    if (const std::int32_t o = global_override(extra_color); o >= 0) return o;

    switch (kind) {
        case 4: return 9;
        case 1:
        case 14:
            switch (extra_color) {
                case 5: return 7;
                case 2: return 6;
                case 8: return 5;
                case 11: return 4;
                default: return 0;
            }
        case 5:
        case 8:
        case 12: return 6;
        case 10: return 7;
        default: return group_slot(extra_color);
    }
}

bool is_special_ground(std::int32_t ground_type) {
    return ground_type == 0 || ground_type == 7 || ground_type == 12;
}

}  // namespace sh::world
