// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where the running game's data is.
#pragma once

#include <string>

extern "C" const char* PcPort_GetGameDataPath(void);

namespace sh::app {

// The game data directory: <exe>/gamedata, the config's gamedata_dir or -data. Set by the start once the first run
// has settled it; PcPort_GetGameDataPath() (the name the engine asks by) answers "./gamedata" before that.
void set_game_data_dir(const std::string& dir);

}  // namespace sh::app
