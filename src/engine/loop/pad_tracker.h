#pragma once

// Controller state of a frame: from what the pad reports this
// frame (buttons, the two analog sticks) and the state of the previous frame, works out which
// buttons are held, newly pressed and released, the auto-repeat ("pulsed") presses for menus,
// the stick deflections past the dead zone and, for the controller the player walks with, a
// steering value the d-pad fills in when the left stick is centred.
//
// Which bit is which button and all the tuning numbers come from the game (PadSpec).
//
// Bit-exactness contract: with the same pad reports, previous state, frame length and run
// button, the state equals the original's to the last bit.

#include <array>
#include <cstdint>

namespace sh::loop {

// Stick axes in the order the pad reports them.
enum StickAxis : int { kRightX = 0, kRightY = 1, kLeftX = 2, kLeftY = 3 };

// Directions in the order the button bits use them.
enum PadDirection : int { kUp = 0, kRight = 1, kDown = 2, kLeft = 3 };

struct PadSpec {
    std::array<std::uint32_t, 4> dpad{};         // d-pad button bits by direction
    std::array<std::uint32_t, 4> left_analog{};  // left stick pushed past the dead zone, by direction
    std::array<std::uint32_t, 4> right_analog{}; // right stick pushed past the dead zone, by direction
    std::array<std::uint32_t, 4> direction{};    // d-pad or left stick, by direction (opposites cancel)
    int dead_zone = 0;            // stick deflection (-128..127) that still counts as centred, exclusive
    int repeat_delay = 0;         // vertical blanks a held button waits before it repeats
    int repeat_interval = 0;      // vertical blanks between repeats
    int dpad_stick = 0;           // stick deflection the d-pad stands for when the left stick is centred
    int dpad_steer = 0;           // steering the d-pad stands for (forward and sideways)
    int dpad_steer_back_walk = 0; // steering of d-pad up without the run button
    int dpad_steer_back_run = 0;  // ... and with it
};

// What the pad reports this frame.
struct PadInput {
    bool connected = false;         // false: no buttons are held
    bool analog = false;            // the pad is in analog mode: the sticks count
    std::uint16_t buttons_up = 0;   // button bits, 0 = pressed (as the pad sends them)
    std::array<std::uint8_t, 4> sticks{};  // raw stick positions 0..255 (128 = centre), by StickAxis
};

// The state the game reads; also the previous frame's state for the next update.
struct PadState {
    std::int32_t repeat_ticks = 0;
    std::uint32_t held = 0;
    std::uint32_t pressed = 0;
    std::uint32_t released = 0;
    std::uint32_t pulsed = 0;       // pressed this frame, or repeating while held
    std::uint32_t pulsed_menu = 0;  // pulsed, without opposite or diagonal directions
    std::array<std::int8_t, 4> sticks_raw{};  // signed stick positions in analog mode, else 0
    std::array<std::int8_t, 4> sticks{};      // deflection past the dead zone (d-pad filled in when steering)
    std::array<std::int8_t, 4> steer{};       // steering of the walking controller
};

class PadTracker {
public:
    explicit PadTracker(const PadSpec& spec) : spec_(spec) {}

    // `vblanks` is the length of the frame, `steering` is true for the controller the player
    // walks with, `run_buttons` are the button bits bound to run.
    void update(PadState& state, const PadInput& in, int vblanks, bool steering, std::uint32_t run_buttons) const;

private:
    std::uint32_t read_sticks(PadState& state, const PadInput& in, std::uint32_t held) const;
    void fill_steering(PadState& state, std::uint32_t held, std::uint32_t run_buttons) const;
    std::uint32_t without_opposites(std::uint32_t bits, int a, int b) const;

    PadSpec spec_;
};

// Presses seen on frames presented between two game updates. The pad report is sampled on every presented frame; the
// game reads it once an update. A button that went down and came up again in between still reaches the next update,
// as one press.
class ButtonLatch {
public:
    // A sample of the report's button word (0: down) on a frame without an update.
    void seen(std::uint16_t buttons_up) { down_ = static_cast<std::uint16_t>(down_ | ~buttons_up); }
    // The button word the update reads: the sample of its own frame with the latched presses; the latch empties.
    std::uint16_t take(std::uint16_t buttons_up) {
        const std::uint16_t out = static_cast<std::uint16_t>(buttons_up & ~down_);
        down_ = 0;
        return out;
    }

private:
    std::uint16_t down_ = 0;
};

}  // namespace sh::loop
