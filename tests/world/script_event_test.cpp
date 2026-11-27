// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// A script's own event (engine/world/script_event.h, sh.event.run): one at a time; asked for, then started by the
// game, then called once a tick until its function says it is over.

#include "engine/world/script_event.h"

#include "framework/sh_test.h"

namespace sw = sh::world;

SH_TEST(script_event_one_at_a_time_from_request_to_end) {
    sw::script_event_end();
    SH_CHECK(!sw::script_event_active() && !sw::script_event_requested());
    SH_CHECK(sw::script_event_tick());  // nothing runs: over
    SH_CHECK(!sw::script_event_request(nullptr));

    int calls = 0;
    SH_CHECK(sw::script_event_request([&calls] { return ++calls == 3; }));
    SH_CHECK(sw::script_event_requested() && sw::script_event_active());
    SH_CHECK(!sw::script_event_request([] { return true; }));  // one is asked for already
    SH_CHECK(sw::script_event_tick() && calls == 0);            // not started: its function is not called

    sw::script_event_begin();
    SH_CHECK(!sw::script_event_requested() && sw::script_event_active());
    SH_CHECK(!sw::script_event_request([] { return true; }));  // one runs
    SH_CHECK(!sw::script_event_tick() && !sw::script_event_tick() && calls == 2);
    SH_CHECK(sw::script_event_tick() && calls == 3);
    sw::script_event_end();
    SH_CHECK(!sw::script_event_active());

    // The function may end the event itself (a map change inside it).
    SH_CHECK(sw::script_event_request([] {
        sw::script_event_end();
        return false;
    }));
    sw::script_event_begin();
    SH_CHECK(!sw::script_event_tick());
    SH_CHECK(!sw::script_event_active());
}
