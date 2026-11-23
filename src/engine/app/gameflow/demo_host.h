// SPDX-License-Identifier: GPL-3.0-or-later
//
// The title screen's demo playback (sh::gameflow::DemoPlayer) on the session:
// the rules come from gamedata/config/demos.json, the host reads and writes the session, the file queue and the
// game's random numbers.
#pragma once

#include "engine/gameflow/demo_player.h"

namespace sh::gameflow {

DemoPlayer& demo_player();
DemoHost& demo_host();

// The setup file's save becomes the running save.
void demo_save_apply();

}  // namespace sh::gameflow
