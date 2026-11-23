// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Per-frame camera-mode arbiter of the main loop: which of the free camera, the third-person
// camera and the developer keys runs this frame. The host answers the
// state questions live because the free camera's own switch-off changes the answer.

#pragma once

#include "engine/game_state/session.h"

namespace sh::gameflow {

class DebugCameraHost {
public:
    virtual ~DebugCameraHost() = default;
    virtual bool keyboard_ready() const = 0;
    virtual bool dev_controls_allowed() const = 0;
    virtual bool console_or_debug_menu_open() const = 0;
    virtual bool in_game() const { return sh::game::session().game_state() == sh::game::game_state::kInGame; }
    virtual bool free_camera_on() const = 0;
    virtual bool third_person_on() const = 0;
    virtual bool alt_camera_state_ok() const = 0;
    virtual void free_camera_fly() = 0;
    virtual void free_camera_apply() = 0;
    virtual void free_camera_set(bool on) = 0;
    virtual void third_person_apply() = 0;
    virtual void dev_keys_update() = 0;
};

void debug_camera_update(DebugCameraHost& host);

}  // namespace sh::gameflow
