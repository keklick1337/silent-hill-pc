// SPDX-License-Identifier: GPL-3.0-or-later
//
// The key list shown while the free camera flies: what moves it, its speed and its modes. H hides it.
#pragma once

namespace sh::freecam {

// Queues the panel on the overlay. Called after the frame has been captured, so the panel never lands in a picture
// the camera is taking.
void draw_controls_panel();

}  // namespace sh::freecam
