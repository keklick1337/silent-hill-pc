#include "engine/loop/reset_watch.h"

namespace sh::loop {

const char* ResetWatch::check(const ResetInput& in, const ResetCounters& counters) const {
    if (in.blocked) {
        return nullptr;
    }
    if (in.demo_playing) {
        if (*counters.demo_vblanks > spec_.demo_vblanks) {
            return "demo-timeout";
        }
    } else {
        *counters.demo_vblanks = 0;
    }
    if (in.at_main_menu) {
        return nullptr;
    }

    // The hold timer runs on the vertical blank; letting go of the combination starts it again.
    if ((in.held & spec_.hold_buttons) != spec_.hold_buttons) {
        *counters.hold_vblanks = 0;
    }
    if (*counters.hold_vblanks > spec_.hold_vblanks) {
        return spec_.hold_name.c_str();
    }
    for (const ResetCombo& combo : spec_.combos) {
        if (in.held == combo.held && (in.pressed & combo.pressed) != 0) {
            return combo.name.c_str();
        }
    }
    if (in.requested) {
        return "flag";
    }
    return nullptr;
}

}  // namespace sh::loop
