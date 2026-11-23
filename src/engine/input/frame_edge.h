// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Frame-stable rising-edge cache for raw key and controller-button queries.
// The caller owns the frame identity:
// a monotonic counter handed to every query of the frame.
//
// Why a cache at all: a bind queried from several places in one frame must give
// all callers the same rising-edge answer. Sampling prev-state per call would
// let the second caller of a frame see the edge of a press the first caller
// already consumed (and a fresh slot started with prev=0 would fire a phantom
// edge for a key that has been held all along, e.g. closing the inventory the
// frame it opened).
//
// Semantics:
//  - the first query of a key seeds prev with the CURRENT held state and is
//    never an edge (a key already down when first asked did not just go down);
//  - the held state is resampled at most once per frame per key: later queries
//    in the same frame return the cached edge;
//  - 16 slots, no eviction: a 17th distinct key is answered false forever;
//  - one instance per physical device (keyboard and controller separately),
//    because the pad reads the physical controller, not the kb-merged pad.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sh::input {

class FrameEdgeCache {
public:
    static constexpr std::size_t kCapacity = 16;

    // `key` is the caller's raw id (SDL scancode / gamepad button); `held` is
    // its current physical state; `frame` is the monotonic frame counter.
    // True only on the frame the key transitions released -> pressed.
    bool edge(int key, bool held, std::int32_t frame);

    static constexpr std::size_t capacity() { return kCapacity; }

private:
    struct Slot {
        int key = -1;
        bool prev = false;
        bool edge = false;
        std::int32_t frame = 0;
    };

    std::array<Slot, kCapacity> slots_;
    std::size_t count_ = 0;
};

}  // namespace sh::input
