// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// A script's own event (sh.event.run): a function of a script that the game runs as it runs a handler of the map's
// events, once a tick inside the game's update while the game is in the event state. The registry holds the one
// function asked for; the game starts it when no other event runs (gameflow/game/map_events_host.cpp), calls it
// from the event state (game_states_host.cpp) and ends it when the function says it is done.

#pragma once

#include <functional>

namespace sh::world {

// One tick of the script's function; true when the event is over.
using ScriptEventTick = std::function<bool()>;

// Asks for the event. False when one is asked for or runs already.
bool script_event_request(ScriptEventTick tick);
// Asked for and not started yet.
bool script_event_requested();
// Asked for or running.
bool script_event_active();
// The game started it.
void script_event_begin();
// One tick; true when the event is over (the function said so, failed, or is gone).
bool script_event_tick();
// Forgets it (it ended, its script closed, another event took its place).
void script_event_end();

}  // namespace sh::world
