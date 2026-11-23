// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The rebinding screen's devices: navigation and capture (bind_capture.h) fed from the keyboard, the mouse and
// the controllers as they are right now, not from the game's pad word, and the names of keys on the keyboard
// layout in use.

#pragma once

#include "input/bind/bind_capture.h"
#include "input/bind/bind_table.h"

#include <string>
#include <string_view>

namespace sh::input {

// Arrows, Enter/Space, Escape, Delete/Backspace, Q/E and Page Up/Down; the left stick, the d-pad, A, B/Start, X
// and the shoulders; and the player's own confirm and cancel binds.
NavState poll_menu_nav(MenuNav& nav);

// Starts listening for the new input of a cell of the bind table.
void begin_bind_capture(int row, BindColumn column, CaptureFilter filter);
BindCapture::Result update_bind_capture();

bool controller_connected();

// A saved key is a keyboard position under its US name, so one config means the same physical keys on every
// layout. These go between that name and what the active layout prints on the key (QWERTZ prints Y on the key
// saved as "Z"). A name that is not a key (Mouse1, MouseWheelUp) comes back as it is.
std::string key_cap_of(std::string_view saved_name);
// Keyboard position of the key with this cap, 0 when there is none.
int position_of_key_cap(std::string_view cap);

}  // namespace sh::input
