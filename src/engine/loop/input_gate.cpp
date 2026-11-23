#include "engine/loop/input_gate.h"

namespace sh::loop {

void InputGate::clear_buttons(PadState& pad) {
    pad.held = 0;
    pad.pressed = 0;
    pad.released = 0;
    pad.pulsed = 0;
    pad.pulsed_menu = 0;
}

void InputGate::free_camera_take(PadState& pad) {
    clear_buttons(pad);
}

bool InputGate::console_take(PadState& pad, bool typing, bool swallowing, bool release_keys_down,
                             std::uint32_t start_button) {
    if (!typing && !swallowing) {
        return false;
    }
    // Released only when both the keys and the pad they drive are clear: the pad lags the keys by
    // a frame, and a stale Start would read as a fresh press.
    if (!typing && !release_keys_down && !(pad.held & start_button)) {
        return false;
    }
    clear_buttons(pad);
    pad.sticks_raw = {};
    pad.sticks = {};
    return swallowing;
}

}  // namespace sh::loop
