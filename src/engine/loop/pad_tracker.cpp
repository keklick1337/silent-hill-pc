#include "engine/loop/pad_tracker.h"

namespace sh::loop {

std::uint32_t PadTracker::without_opposites(std::uint32_t bits, int a, int b) const {
    const std::uint32_t pair = spec_.direction[a] | spec_.direction[b];
    return (bits & pair) == pair ? bits & ~pair : bits;
}

std::uint32_t PadTracker::read_sticks(PadState& state, const PadInput& in, std::uint32_t held) const {
    state.sticks_raw = {};
    state.sticks = {};
    if (!in.analog) {
        return held;
    }
    for (int axis = 0; axis < 4; ++axis) {
        const bool vertical = axis == kRightY || axis == kLeftY;
        const auto& bits = axis >= kLeftX ? spec_.left_analog : spec_.right_analog;
        const int value = static_cast<int>(in.sticks[axis]) - 128;
        state.sticks_raw[axis] = static_cast<std::int8_t>(value);
        if (value < -spec_.dead_zone) {
            state.sticks[axis] = static_cast<std::int8_t>(value + spec_.dead_zone);
            held |= bits[vertical ? kUp : kLeft];
        } else if (value >= spec_.dead_zone) {
            state.sticks[axis] = static_cast<std::int8_t>(value - (spec_.dead_zone - 1));
            held |= bits[vertical ? kDown : kRight];
        }
    }
    return held;
}

void PadTracker::fill_steering(PadState& state, std::uint32_t held, std::uint32_t run_buttons) const {
    // Only a single d-pad direction on an axis counts; the stick wins when it is pushed.
    const std::uint32_t vertical = held & (spec_.dpad[kUp] | spec_.dpad[kDown]);
    const std::uint32_t horizontal = held & (spec_.dpad[kRight] | spec_.dpad[kLeft]);
    const bool stick_y_centred = state.steer[kLeftY] == 0;
    const bool stick_x_centred = state.steer[kLeftX] == 0;

    if (stick_y_centred) {
        if (vertical == spec_.dpad[kDown]) {
            state.sticks[kLeftY] = static_cast<std::int8_t>(spec_.dpad_stick);
            state.steer[kLeftY] = static_cast<std::int8_t>(spec_.dpad_steer);
        } else if (vertical == spec_.dpad[kUp]) {
            state.sticks[kLeftY] = static_cast<std::int8_t>(-spec_.dpad_stick);
            state.steer[kLeftY] = static_cast<std::int8_t>((held & run_buttons) != 0 ? spec_.dpad_steer_back_run
                                                                                     : spec_.dpad_steer_back_walk);
        }
    }
    if (stick_x_centred) {
        if (horizontal == spec_.dpad[kRight]) {
            state.sticks[kLeftX] = static_cast<std::int8_t>(spec_.dpad_stick);
            state.steer[kLeftX] = static_cast<std::int8_t>(spec_.dpad_steer);
        } else if (horizontal == spec_.dpad[kLeft]) {
            state.sticks[kLeftX] = static_cast<std::int8_t>(-spec_.dpad_stick);
            state.steer[kLeftX] = static_cast<std::int8_t>(-spec_.dpad_steer);
        }
    }
}

void PadTracker::update(PadState& state, const PadInput& in, int vblanks, bool steering,
                        std::uint32_t run_buttons) const {
    const std::uint32_t previous = state.held;

    std::uint32_t held = in.connected ? (~static_cast<std::uint32_t>(in.buttons_up) & 0xFFFFu) : 0u;
    held = read_sticks(state, in, held);
    state.steer = state.sticks;
    if (steering) {
        fill_steering(state, held, run_buttons);
    }

    for (int d = 0; d < 4; ++d) {
        if (held & (spec_.dpad[d] | spec_.left_analog[d])) {
            held |= spec_.direction[d];
        }
    }
    held = without_opposites(held, kUp, kDown);
    held = without_opposites(held, kRight, kLeft);

    state.held = held;
    state.pressed = ~previous & held;
    state.released = previous & ~held;

    std::int32_t ticks = held != previous ? 0 : state.repeat_ticks + vblanks;
    if (ticks >= spec_.repeat_delay) {
        state.pulsed = held;
        ticks = spec_.repeat_delay - spec_.repeat_interval;
    } else {
        state.pulsed = state.pressed;
    }
    state.repeat_ticks = ticks;

    // Menus move along one axis at a time: opposite directions cancel, vertical beats horizontal.
    std::uint32_t menu = without_opposites(state.pulsed, kRight, kLeft);
    menu = without_opposites(menu, kUp, kDown);
    if (menu & (spec_.direction[kUp] | spec_.direction[kDown])) {
        menu &= ~(spec_.direction[kRight] | spec_.direction[kLeft]);
    }
    state.pulsed_menu = menu;
}

}  // namespace sh::loop
