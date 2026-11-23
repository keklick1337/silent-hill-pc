#pragma once

// Input gate of the game loop: after the pad has been read, an
// overlay may own it for the frame and nothing underneath may react.
//   - The free camera flies with the same keys Harry walks with: the pad is cleared.
//   - The console clears the pad, sticks included, while typing and afterwards until the submit and
//     exit keys are released, so the final Enter cannot reach the game as Start.
// Clearing happens right after the pad update: cleared later, the next update would refill it
// before anything read it.

#include <cstdint>

#include "engine/loop/pad_tracker.h"

namespace sh::loop {

class InputGate {
public:
    static void free_camera_take(PadState& pad);

    // `typing`: the console line is open; `swallowing`: the release is still pending. Returns
    // whether the release is still pending after this frame. `release_keys_down`: a submit or exit
    // key is down (or the keyboard cannot be read); `start_button` is the pad bit those keys map to.
    static bool console_take(PadState& pad, bool typing, bool swallowing, bool release_keys_down,
                             std::uint32_t start_button);

private:
    static void clear_buttons(PadState& pad);
};

}  // namespace sh::loop
