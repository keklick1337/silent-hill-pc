// The `mods` console command: the registry report, a mod's files, the pending edits and the hot reload
// (mods_admin.h). Its own small library so the console and the mod registry stay independent of each other.
#pragma once

#include "engine/console/registry.h"

namespace sh::mods {

void register_console_commands(sh::console::Registry& console);

}  // namespace sh::mods
