// SPDX-License-Identifier: GPL-3.0-or-later
// `mods reload` of the parsed game data (mods_data_reload.cpp).
#pragma once

#include <string>

namespace sh::app {

// After hot_reload found changed data files (sh::mods::last_reload_changes): asks for the data reload at the main
// frame's next warm reset point. Returns the line `mods reload` shows.
std::string request_data_reload();

}  // namespace sh::app

extern "C" {
// The main frame's warm reset point: the reload is due (in play outside events and menus, or on the title) / runs it.
int Pc_DataReload_Due(void);
void Pc_DataReload_Run(void);
}
