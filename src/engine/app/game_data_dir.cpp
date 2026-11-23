// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/app/game_data_dir.h"

#include <cstdio>

namespace {

// One fixed place: callers keep the pointer PcPort_GetGameDataPath() gave them.
char g_data_dir[1024] = "./gamedata";

}  // namespace

extern "C" const char* PcPort_GetGameDataPath(void) { return g_data_dir; }

namespace sh::app {

void set_game_data_dir(const std::string& dir) { std::snprintf(g_data_dir, sizeof(g_data_dir), "%s", dir.c_str()); }

}  // namespace sh::app
