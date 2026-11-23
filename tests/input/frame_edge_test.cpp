// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include "engine/input/frame_edge.h"

using namespace sh::input;

SH_TEST(frame_edge_first_query_seeds_and_is_never_an_edge) {
    FrameEdgeCache c;
    // Key already held when first queried: no edge (it did not just go down).
    SH_CHECK(!c.edge(30, true, 1));
    // Still held next frame: no edge.
    SH_CHECK(!c.edge(30, true, 2));
    // Released and pressed again: edge on the press frame only.
    SH_CHECK(!c.edge(30, false, 3));
    SH_CHECK(c.edge(30, true, 4));
    SH_CHECK(!c.edge(30, true, 5));
}

SH_TEST(frame_edge_same_frame_returns_cached_result) {
    FrameEdgeCache c;
    SH_CHECK(!c.edge(7, false, 100));
    SH_CHECK(c.edge(7, true, 101));
    // A second caller in the SAME frame sees the same edge, and a third query
    // with a different held value must not resample (once per frame per slot).
    SH_CHECK(c.edge(7, true, 101));
    SH_CHECK(c.edge(7, false, 101));  // cached, not a resample: still the edge
    // Next frame samples fresh state.
    SH_CHECK(!c.edge(7, false, 102));
}

SH_TEST(frame_edge_frame_counter_may_jump_or_stall) {
    FrameEdgeCache c;
    SH_CHECK(!c.edge(1, false, 5));
    // Same frame id repeated across real frames (caller stalled): the slot is
    // NOT resampled, matching the once-per-frame-id contract.
    SH_CHECK(!c.edge(1, true, 5));
    SH_CHECK(!c.edge(1, true, 5));
    // A big jump is just another frame id. The release is not an edge (prev is
    // still false — the held=true at frame 5 was never sampled), so the press
    // after it is.
    SH_CHECK(!c.edge(1, false, 900));
    SH_CHECK(c.edge(1, true, 1000000));
}

SH_TEST(frame_edge_capacity_is_sixteen_without_eviction) {
    FrameEdgeCache c;
    for (int k = 0; k < 16; k++) {
        SH_CHECK(!c.edge(k, false, 1));
    }
    // The 17th distinct key is answered false forever, and it does not disturb
    // the sixteen resident slots.
    SH_CHECK(!c.edge(16, true, 2));
    SH_CHECK(!c.edge(16, false, 3));
    SH_CHECK(c.edge(16, true, 4) == false);
    SH_CHECK(c.edge(5, true, 2));
    SH_CHECK(!c.edge(5, true, 3));
}

SH_TEST(frame_edge_instances_are_independent) {
    // Keyboard and controller keep separate caches.
    FrameEdgeCache keys;
    FrameEdgeCache pad;
    SH_CHECK(!keys.edge(30, false, 1));
    SH_CHECK(!pad.edge(30, false, 1));
    SH_CHECK(keys.edge(30, true, 2));
    SH_CHECK(pad.edge(30, true, 2));
    SH_CHECK(!keys.edge(30, true, 3));
    SH_CHECK(!pad.edge(30, true, 3));
}
