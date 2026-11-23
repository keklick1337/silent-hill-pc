// SPDX-License-Identifier: GPL-3.0-or-later
// The options' controller entry: the game's native controls screen (bindings are edited
// in Options > Controls) shown with the options' fades. While it stays open the menu input of the
// frame is consumed; when it closes the screen fades out and returns to the main
// options. The screen itself is the host's.

#pragma once

#include <cstdint>

#include "engine/screens/staff_roll.h"

namespace sh::screens {

class ControlsScreenHost {
public:
    virtual ~ControlsScreenHost() = default;

    // One frame of the native screen; false once it closed.
    virtual bool update_screen() = 0;
    virtual void draw_screen() = 0;
    virtual void close_screen() = 0;
    virtual void show_mouse_cursor() = 0;
    virtual void consume_menu_input() = 0;

    virtual std::int32_t step(int level) = 0;
    virtual void set_step(int level, std::int32_t value) = 0;
    virtual Fade fade() = 0;
    virtual void set_fade(Fade fade) = 0;
    virtual void leave() = 0;  // back to the main options
};

class ControlsScreen {
public:
    // Screen step (gameStateSteps[1]) while it fades out.
    static constexpr std::int32_t kStepLeave = -2;

    void update(ControlsScreenHost& host) const;
};

}  // namespace sh::screens
