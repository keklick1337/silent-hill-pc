// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Aids of scripted checks that need no game: the pad buttons a script names ("Up+Cross"), and the dice that
// decide which updates a `stall` test sleeps through (its own generator: the game's random numbers stay as
// they are).

#pragma once

#include "formats/common/status.h"

#include <cstdint>
#include <string_view>

namespace sh::debug {

// The pad word's bits of the named buttons, joined with '+' (names as replay routes spell them, any case;
// "-" names none). Fails with the first unknown name as the error text.
Result<std::uint16_t> parse_pad_buttons(std::string_view text);

class StallDice {
public:
    // `percent` of the updates stall (clamped to 0..100); the seed fixes which ones.
    void configure(int percent, std::uint32_t seed);
    int percent() const { return percent_; }
    // Milliseconds this update sleeps: 0 for most, 40..100 for the chosen share.
    std::uint32_t roll();

private:
    std::uint32_t next();

    int percent_ = 0;
    std::uint32_t state_ = 1;
};

}  // namespace sh::debug
