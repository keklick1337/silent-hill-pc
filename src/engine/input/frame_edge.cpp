// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/input/frame_edge.h"

namespace sh::input {

bool FrameEdgeCache::edge(int key, bool held, std::int32_t frame) {
    std::size_t slot = count_;
    for (std::size_t i = 0; i < count_; i++) {
        if (slots_[i].key == key) {
            slot = i;
            break;
        }
    }
    if (slot == count_) {
        if (count_ >= kCapacity) return false;
        slot = count_++;
        slots_[slot].key = key;
        // Seed prev with the current held state: a key already down when first
        // queried did not just go down.
        slots_[slot].prev = held;
        slots_[slot].edge = false;
        slots_[slot].frame = frame;
        return false;
    }

    // Resample only once per frame (per slot): other callers in the same frame
    // get the cached edge result.
    if (slots_[slot].frame != frame) {
        slots_[slot].edge = held && !slots_[slot].prev;
        slots_[slot].prev = held;
        slots_[slot].frame = frame;
    }
    return slots_[slot].edge;
}

}  // namespace sh::input
