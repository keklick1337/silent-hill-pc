// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include "engine/items/weapon_cycle.h"

#include <vector>

using sh::items::weapon_cycle_next;

namespace {

struct Owns {
    std::vector<int> ids;
};

int owned(void* ctxp, int item) {
    Owns* o = static_cast<Owns*>(ctxp);
    for (int id : o->ids)
        if (id == item) return 1;
    return 0;
}

}  // namespace

SH_TEST(weapon_cycle_walks_forward_and_wraps) {
    const int order[] = {10, 11, 12, 13, 14};
    Owns all{{10, 11, 12, 13, 14}};
    SH_CHECK(weapon_cycle_next(order, 5, 0, 1, owned, &all) == 1);
    SH_CHECK(weapon_cycle_next(order, 5, 4, 1, owned, &all) == 0);  // wraps
    // Not listed: the walk starts at the front.
    SH_CHECK(weapon_cycle_next(order, 5, -1, 1, owned, &all) == 0);
}

SH_TEST(weapon_cycle_walks_backward) {
    const int order[] = {10, 11, 12, 13, 14};
    Owns all{{10, 11, 12, 13, 14}};
    SH_CHECK(weapon_cycle_next(order, 5, 3, -1, owned, &all) == 2);
    SH_CHECK(weapon_cycle_next(order, 5, 0, -1, owned, &all) == 4);  // wraps
    // Wheel's unlisted rule for -1: start AT the front, so the first visit is the last entry.
    SH_CHECK(weapon_cycle_next(order, 5, 0, -1, owned, &all) == 4);
}

SH_TEST(weapon_cycle_skips_unowned) {
    const int order[] = {10, 11, 12, 13, 14};
    Owns sparse{{12, 14}};
    SH_CHECK(weapon_cycle_next(order, 5, 0, 1, owned, &sparse) == 2);   // 10,11 not held
    SH_CHECK(weapon_cycle_next(order, 5, 2, 1, owned, &sparse) == 4);
    SH_CHECK(weapon_cycle_next(order, 5, 4, 1, owned, &sparse) == 2);   // wraps, skips 10,11,13
    // From index 1 backward: 0 (10, unowned), then 4 (14, owned).
    SH_CHECK(weapon_cycle_next(order, 5, 1, -1, owned, &sparse) == 4);
    Owns none{};
    SH_CHECK(weapon_cycle_next(order, 5, 2, 1, owned, &none) == -1);
    SH_CHECK(weapon_cycle_next(order, 5, -1, -1, owned, &none) == -1);
}

SH_TEST(weapon_cycle_degenerate_orders) {
    const int one[] = {7};
    Owns all{{7}};
    SH_CHECK(weapon_cycle_next(one, 1, 0, 1, owned, &all) == 0);
    SH_CHECK(weapon_cycle_next(one, 1, -1, -1, owned, &all) == 0);
    SH_CHECK(weapon_cycle_next(nullptr, 0, 0, 1, owned, &all) == -1);
    SH_CHECK(weapon_cycle_next(one, 1, 0, 1, nullptr, nullptr) == -1);
}
