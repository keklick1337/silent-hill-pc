// SPDX-License-Identifier: GPL-3.0-or-later
//
// The staff roll's vblank clock: the roll reads it, the ending scenes add the vblanks to it (map variable
// "credits_vblank_clock"), and the loader sets it to 1 when a streamed chunk lands, as the game's shared word did.
#pragma once

#include <cstdint>

namespace sh::screens {

std::int32_t& staff_roll_clock();

}  // namespace sh::screens
