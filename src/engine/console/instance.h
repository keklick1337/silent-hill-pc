// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The process-wide command registry: the exe console, engine modules and Lua mods register into this one
// instance. Not thread-safe; everything runs on the main thread.

#pragma once

#include "engine/console/registry.h"

namespace sh::console {

Registry& global_registry();

// Where sh.console.print writes; the Console installs itself here. Null means "no console yet".
Output* active_output();
void set_active_output(Output* out);

}  // namespace sh::console
