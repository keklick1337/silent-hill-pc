// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/world/script_event.h"

#include <utility>

namespace sh::world {

namespace {

struct ScriptEvent {
    ScriptEventTick tick;
    bool requested = false;
    bool running = false;
};

ScriptEvent& event() {
    static ScriptEvent e;
    return e;
}

}  // namespace

bool script_event_request(ScriptEventTick tick) {
    ScriptEvent& e = event();
    if (!tick || e.requested || e.running) {
        return false;
    }
    e.tick = std::move(tick);
    e.requested = true;
    return true;
}

bool script_event_requested() { return event().requested; }

bool script_event_active() { return event().requested || event().running; }

void script_event_begin() {
    ScriptEvent& e = event();
    if (e.requested) {
        e.requested = false;
        e.running = true;
    }
}

bool script_event_tick() {
    ScriptEvent& e = event();
    if (!e.running || !e.tick) {
        return true;
    }
    // The function may end the event itself (a map change): it is called through a copy.
    const ScriptEventTick tick = e.tick;
    return tick();
}

void script_event_end() { event() = ScriptEvent{}; }

}  // namespace sh::world
