#pragma once

// Free camera of the developer tools: a photo-mode camera that
// flies over the world while the game holds still. Mouse or right stick look, W/A/S/D or the left
// stick move, E/Q (C) or R2/L2 rise and sink, the wheel scales the speed, Shift or R1 is fast,
// Ctrl or L1 slow; "steady" eases the look and keeps the travel direction, "cruise" keeps the last
// velocity. Movement eases toward the wanted velocity so pans and dollies stay smooth.
//
// Units are the world's: positions in Q12 units, angles in Q12 turns (4096 a turn), +Y down.
// The host reads the keys, mouse and pad (FreeCameraInput) and applies the view (look_at()).

#include <array>
#include <cstdint>

namespace sh::devtools {

struct FreeCameraKeys {
    bool hud = false;     // toggles the controls panel
    bool cruise = false;  // toggles cruise
    bool steady = false;  // held
    bool fast = false;
    bool slow = false;
    bool forward = false, back = false, left = false, right = false;
    bool rise = false, sink = false;
};

struct FreeCameraPad {
    bool present = false;
    int right_x = 128, right_y = 128, left_x = 128, left_y = 128;  // raw 0..255, 128 = centre
    bool fast = false, slow = false, rise = false, sink = false;
};

struct FreeCameraInput {
    std::int32_t delta_time = 0;  // Q12 seconds of this frame
    FreeCameraKeys keys;
    int wheel_notches = 0;
    int mouse_dx = 0, mouse_dy = 0;
    float mouse_sensitivity = 1.0f;
    bool invert_mouse_y = false;
    FreeCameraPad pad;
    float pad_sensitivity = 1.0f;
    bool invert_pad_y = false;
};

class FreeCamera {
public:
    // Starts from the given view; keys already down do not toggle anything until released.
    void start(const std::array<std::int32_t, 3>& position, std::int32_t yaw);
    void update(const FreeCameraInput& in);
    // Turns the view to a heading and pitch in angle units, fractions kept (the editor's placement).
    void aim(double yaw_units, double pitch_units);

    // A point 5 units ahead along the view.
    std::array<std::int32_t, 3> look_at() const;

    // The view's heading and pitch before their rounding to whole angle units, and the position with the fraction
    // of a unit it has not moved yet: what the renderer's unrounded camera is built from.
    double exact_yaw() const { return yaw_f_; }
    double exact_pitch() const { return pitch_f_; }
    std::array<double, 3> exact_position() const;

    std::array<std::int32_t, 3> position{};
    std::int32_t yaw = 0;    // Q12 turns
    std::int32_t pitch = 0;  // Q12 turns, positive looks down
    bool hud_hidden = false;
    bool steady = false;
    bool cruise = false;
    float speed_scale = 1.0f;

private:
    bool edge(bool down, int slot);

    std::array<float, 3> velocity_{};
    std::array<float, 3> remainder_{};  // the fraction of a unit each axis moved and the integer position did not
    std::array<float, 3> cruise_velocity_{};
    float yaw_f_ = 0.0f, pitch_f_ = 0.0f;
    std::array<float, 2> look_rate_{};
    float track_yaw_ = 0.0f, track_pitch_ = 0.0f;
    std::array<bool, 2> key_was_down_{};
};

}  // namespace sh::devtools
