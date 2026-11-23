// Console commands of the hook bus: `hooks` (the catalog with handlers and counts) and `hooks_trace [on|off]`.
#pragma once

#include "engine/console/registry.h"

namespace sh::hooks {

void register_console_commands(sh::console::Registry& console);

}  // namespace sh::hooks
