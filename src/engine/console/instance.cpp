// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/console/instance.h"

namespace sh::console {

namespace {
Output* g_active_output = nullptr;
}

Registry& global_registry() {
    // Leaked on purpose: Lua hosts living in other statics unregister their commands in their destructors,
    // which run in an unspecified order at exit.
    static Registry* registry = new Registry;
    return *registry;
}

Output* active_output() { return g_active_output; }
void set_active_output(Output* out) { g_active_output = out; }

}  // namespace sh::console
