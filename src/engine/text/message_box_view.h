// SPDX-License-Identifier: GPL-3.0-or-later
// What the map-message box (src/ui/screens/message_box_host.cpp) needs of the game besides the session.
// Implemented on the game's records.
#pragma once

namespace sh::text {

// A message stops the gas weapons: their power timer runs out and the weapon model goes back to its rest state.
void stop_player_gas_weapon();
// The voice stream's state (the sound driver's streaming check).
int voice_stream_state();
// Uploads the 12x16 font's palette to video memory.
void upload_message_font_palette();

// The box's start state and the font palette, at the game's start (src/ui/screens/message_box_host.cpp).
void init_message_box();

}  // namespace sh::text
