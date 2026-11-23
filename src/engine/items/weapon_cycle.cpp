// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/items/weapon_cycle.h"

namespace sh::items {

int weapon_cycle_next(const int* order, std::size_t count, int start, int direction,
                      int (*owned)(void* ctx, int item), void* ctx) {
    if (order == nullptr || count == 0 || owned == nullptr) return -1;

    const int n = static_cast<int>(count);
    for (int k = 1; k <= n; k++) {
        // ((v % n) + n) % n: the two spellings the callers use ((cur + k) % n with a
        // negative fix-up, and (index + direction*k + 2n) % n) agree with this
        // for every start/direction the callers pass.
        const int idx = ((start + direction * k) % n + n) % n;
        if (owned(ctx, order[idx])) return idx;
    }
    return -1;
}

}  // namespace sh::items
