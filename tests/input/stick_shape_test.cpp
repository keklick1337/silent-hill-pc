// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The stick zones of a pad (src/input/stick_shape.h): the round dead zone, the rescale past it, the rim that reads
// full and the move stick's diagonal reaching full on both axes.

#include "framework/sh_test.h"

#include "input/stick_shape.h"

#include <cmath>
#include <cstdlib>

using namespace sh::input;

namespace {

constexpr StickZones kMove{10, 10, true};
constexpr StickZones kLook{0, 10, false};

int at(double fraction) { return static_cast<int>(std::lround(fraction * 32767.0)); }

// The deflection the game reads (-128..127), as the pad layer hands it over.
int game(int v) { return v / 256; }

bool near(int value, double fraction, int slack = 40) { return std::abs(value - at(fraction)) <= slack; }

}  // namespace

SH_TEST(stick_shape_centre_and_dead_zone_read_still) {
    const StickAxes rest = shape_stick(0, 0, kMove);
    SH_CHECK(rest.x == 0 && rest.y == 0);
    // A worn stick resting 8% off centre, and a push just inside the zone on a diagonal (radius 0.099).
    const StickAxes drift = shape_stick(at(0.08), at(-0.03), kMove);
    SH_CHECK(drift.x == 0 && drift.y == 0);
    const StickAxes inside = shape_stick(at(0.07), at(0.07), kMove);
    SH_CHECK(inside.x == 0 && inside.y == 0);
}

SH_TEST(stick_shape_rescales_from_the_zone_to_the_rim) {
    // Straight up: 0.1 is the edge of the zone, 0.5 is half of the travel left, 0.9 and beyond read full.
    SH_CHECK(shape_stick(0, at(-0.11), kMove).y > -1000);
    SH_CHECK(near(shape_stick(0, at(-0.11), kMove).y, -0.0125));
    SH_CHECK(near(shape_stick(0, at(-0.5), kMove).y, -0.5));
    SH_CHECK(shape_stick(0, at(-0.9), kMove).y == -32768);
    SH_CHECK(shape_stick(0, -32768, kMove).y == -32768);
    SH_CHECK(shape_stick(at(0.95), 0, kMove).x == 32767);
    // Never past full, and the other axis stays centred.
    SH_CHECK(shape_stick(0, at(-0.95), kMove).x == 0);
    // The game's own walk threshold (64 of 128) is crossed at half of the travel the zones leave.
    SH_CHECK(game(shape_stick(0, at(-0.49), kMove).y) > -64);
    SH_CHECK(game(shape_stick(0, at(-0.51), kMove).y) <= -64);
    SH_CHECK(game(shape_stick(0, at(-0.9), kMove).y) == -128);
}

SH_TEST(stick_shape_move_diagonal_reads_full_on_both_axes) {
    // A round gate gives 0.707 on each axis at a full diagonal: the move stick reads it at the corner.
    const double d = std::sqrt(0.5);
    const StickAxes full = shape_stick(at(d), at(-d), kMove);
    SH_CHECK(full.x == 32767 && full.y == -32768);
    // Half way along the diagonal: both axes at half, the direction kept.
    const StickAxes half = shape_stick(at(0.5 * d), at(-0.5 * d), kMove);
    SH_CHECK(near(half.x, 0.5) && near(half.y, -0.5));
    // 30 degrees off straight up at full: the long axis full, the short one at tan(30).
    const StickAxes off = shape_stick(at(0.5), at(-0.866), kMove);
    SH_CHECK(off.y == -32768);
    SH_CHECK(near(off.x, std::tan(30.0 * 3.14159265358979 / 180.0), 80));
}

SH_TEST(stick_shape_look_stays_round) {
    const double d = std::sqrt(0.5);
    const StickAxes full = shape_stick(at(d), at(d), kLook);
    SH_CHECK(near(full.x, d, 60) && near(full.y, d, 60));
    // No inner zone: a small push passes, scaled only by the rim.
    SH_CHECK(near(shape_stick(at(0.09), 0, kLook).x, 0.1));
    SH_CHECK(shape_stick(at(-0.9), 0, kLook).x == -32768);
}

SH_TEST(stick_shape_zones_clamp) {
    // Zones that meet or overlap: any push past the centre is a full one.
    const StickZones step{50, 60, false};
    SH_CHECK(shape_stick(at(0.4), 0, step).x == 0);
    SH_CHECK(shape_stick(at(0.6), 0, step).x == 32767);
    // No zones at all and no square: the axes pass as they are.
    const StickZones none{0, 0, false};
    const StickAxes raw = shape_stick(at(0.3), at(-0.6), none);
    SH_CHECK(near(raw.x, 0.3, 2) && near(raw.y, -0.6, 2));
}
