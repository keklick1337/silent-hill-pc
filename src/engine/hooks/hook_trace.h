// A base-owned handler on every catalog event that writes "[HOOK] <event> {fields}" through a sink: the way to see
// in a game run which events fire and with what (console `hooks_trace on`).
#pragma once

#include "engine/hooks/hook_bus.h"

#include <functional>
#include <string>

namespace sh::hooks {

// "{item=HandgunBullets, count=15}" (strings bare, bools true/false), "{}" without fields.
std::string format_fields(const Event& event);

// Registers (on) or drops (off) the trace handlers (owner "silenthill", id "trace") on `bus`. game.tick and
// input.action are frequent: game.tick is written every `tick_every`th time. Returns the handlers registered.
std::size_t trace_set(Bus& bus, bool on, std::function<void(const std::string& line)> sink, int tick_every = 150);
bool trace_on(const Bus& bus);

}  // namespace sh::hooks
