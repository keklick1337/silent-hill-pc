// SPDX-License-Identifier: GPL-3.0-or-later
// The options' controller entry around the native controls screen.

#include "engine/screens/controls_screen.h"

namespace sh::screens {

void ControlsScreen::update(ControlsScreenHost& host) const {
    if (host.step(1) == kStepLeave) {
        // Fading out: keep drawing the screen until the fade completes.
        host.draw_screen();
        host.show_mouse_cursor();
        if (host.fade().state == FadeState::OutComplete) {
            host.close_screen();
            host.set_fade({FadeState::InStart, false});
            host.leave();
        }
        return;
    }
    const bool open = host.update_screen();
    host.draw_screen();
    host.show_mouse_cursor();
    if (open) {
        host.set_fade({FadeState::InSteps, false});
        host.consume_menu_input();
        return;
    }
    host.set_fade({FadeState::OutSteps, false});
    host.set_step(1, kStepLeave);
    host.set_step(2, 0);
}

}  // namespace sh::screens
