// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The camera the game is played with: the original's own, or one of the alternate ones (third person behind the
// player, the same held over one shoulder, first person). The choice is the config key `control_style`, is made
// on the Options screen and switched in the game with the Change Camera bind, and it also decides which set of
// binds is live (the alternate cameras share one) and whether the mouse is captured to look around.
//
// The first half of this header is plain data and arithmetic; CameraStyles is the state of the running game
// (camera_style_host.cpp).

#pragma once

#include <string>
#include <string_view>

namespace sh::input {

// The numbers are what the configuration holds.
enum class CameraStyle : int { Classic = 0, ThirdPerson = 1, OverShoulder = 2, FirstPerson = 3 };
inline constexpr int kCameraStyles = 4;

// A number outside the list is Classic.
CameraStyle camera_style_from(int value);
// The value of `control_style` in config.cfg: classic, tps, ots, fps.
const char* camera_style_id(CameraStyle style);
constexpr bool is_alternate_camera(CameraStyle style) { return style != CameraStyle::Classic; }

// The next (direction > 0) or previous camera of the round classic -> third person -> first person. Over the
// shoulder is not on the round: it is chosen in config.cfg, and steps on as third person does.
CameraStyle next_camera_style(CameraStyle style, int direction);

// "id:Name|id:Name|...", written to config.cfg as `control_styles` so that someone editing the file sees what
// this build accepts. The cameras of the round only.
std::string camera_style_list();

// The mouse button a bind names: 1..5 for Mouse1..Mouse5 (any case), 0 for anything else.
int mouse_button_of_bind(std::string_view bind);

class CameraStyles {
public:
    // At start: takes the style from the configuration and writes the style list to config.cfg.
    void init();
    // Once a frame: the Change Camera and Swap Shoulder binds, which binds are live, and the mouse capture.
    void frame_update();

    // Makes a style the current one and saves it.
    void set(CameraStyle style);
    CameraStyle style() const { return style_; }
    bool first_person() const { return style_ == CameraStyle::FirstPerson; }
    // +1: the camera is over the right shoulder, -1: over the left.
    int shoulder_side() const { return shoulder_side_; }

    // The alternate camera should start again from behind the player instead of from where it last was.
    void request_camera_reset() { camera_reset_ = true; }
    // True once per request.
    bool take_camera_reset();

    // A bind was changed while the game runs: puts the set of binds that is live right now into effect again.
    void reapply_binds();

private:
    void adopt(CameraStyle style);

    CameraStyle style_ = CameraStyle::Classic;
    int shoulder_side_ = 1;
    bool camera_reset_ = false;
    // Outside free movement the menus are driven with the original camera's binds.
    bool menu_binds_ = false;

    // Binds resolved to device codes, per bind set where the bind has two; redone when a bind changes.
    int resolved_at_ = -1;
    int change_key_[2] = {0, 0};
    int change_pad_[2] = {-1, -1};
    int swap_key_ = 0;
    int swap_mouse_ = 0;
    int swap_pad_ = -1;

    bool change_key_was_down_ = false;
    bool change_pad_was_down_ = false;
    bool swap_was_down_ = false;
};

CameraStyles& camera_styles();

}  // namespace sh::input
