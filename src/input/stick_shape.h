// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// What a pad's stick reports before the game reads it (config.cfg pad_deadzone, pad_outer_deadzone,
// pad_look_deadzone): a round dead zone at the centre, an edge band that counts as full, the travel between them
// stretched over the whole range, and for the move stick the round gate stretched to the corners of a square.
// No SDL, no game: SDL's axis values in, the same scale out.

#pragma once

namespace sh::input {

struct StickZones {
    int inner_percent = 0;  // radius that still reads as centred
    int outer_percent = 0;  // band at the rim that reads as a full push
    bool square = false;    // a full push on a diagonal reads full on both axes
};

struct StickAxes {
    int x = 0;
    int y = 0;
};

// x, y: SDL axis values, -32768..32767. The result is in the same range: 0 inside the inner zone, rising from 0 just
// past it, full (-32768 or 32767 on the longer axis) from the outer band on. The direction is kept.
StickAxes shape_stick(int x, int y, const StickZones& zones);

}  // namespace sh::input
