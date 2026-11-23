// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the item-pickup animation (sh::inventory::pickup_step); expected values follow
// the original's arithmetic.

#include "engine/inventory/pickup_anim.h"

#include "framework/sh_test.h"

using namespace sh::inventory;

SH_TEST(pickup_anim_grows_then_snaps_then_spins) {
    PickupAnim a = pickup_begin();
    SH_CHECK(!pickup_step(a, 68, 512, 0));  // first frame: size 0, tilt held
    SH_CHECK(a.scale == 0);
    SH_CHECK(a.rot_x == 512);
    SH_CHECK(a.grow == 136);
    int frames = 0;
    while (a.state == 0 && frames < 100) {
        pickup_step(a, 68, 512, 0);
        ++frames;
    }
    SH_CHECK(a.state == 1);
    SH_CHECK(a.grow == 2048);
    SH_CHECK(pickup_step(a, 68, 512, 0));  // snap
    SH_CHECK(a.scale == 4096);
    SH_CHECK(a.state == 2);
    pickup_spin(a, 68);
    SH_CHECK(a.rot_y == 34);
    a.rot_y = 32767;
    pickup_spin(a, 2);
    SH_CHECK(a.rot_y == -32768);
}
