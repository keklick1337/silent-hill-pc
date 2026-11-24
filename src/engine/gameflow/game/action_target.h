// SPDX-License-Identifier: GPL-3.0-or-later
// What the action button would start where the player stands now: the map's button event in reach (the trigger scan
// of map_events_host.cpp without its effects). The touch screen shows its action button by it.
#pragma once

#include <string>

namespace sh::gameflow::game {

struct ActionTarget {
    bool found = false;
    int state = 0;        // the game state the event runs (sh::gameflow::GameState)
    int param = 0;        // its parameter: the handler slot of an event callback, the message, ...
    std::string handler;  // the map's handler of an event callback (handlers.json), "" for other events
};

ActionTarget action_target();

}  // namespace sh::gameflow::game
