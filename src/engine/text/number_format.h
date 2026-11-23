// SPDX-License-Identifier: GPL-3.0-or-later
// Decimal formatting for the debug/int string drawers: digits (and the minus sign)
// in reading order, optionally padded on the left. The callers copy the bytes into
// their backing store from the end downward, which is why the formats are described
// left-to-right here instead of being written into a buffer directly.

#pragma once

#include <cstdint>

namespace sh::text {

struct NumberText {
    char bytes[16] = {};   // bytes[0] is the leftmost character
    std::int32_t count = 0;
};

// Decimal digits of |value|, prefixed with '-' when negative. Left-padding character
// is the caller's (the debug font pads with its blank glyph).
NumberText format_decimal(std::int32_t value);
NumberText format_decimal_padded(std::int32_t value, std::int32_t min_width, char pad);

}  // namespace sh::text
