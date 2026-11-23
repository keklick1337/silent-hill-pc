// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/text/number_format.h"

namespace sh::text {

NumberText format_decimal(std::int32_t value) {
    NumberText out;
    // The negate wraps like the original's int arithmetic for the INT_MIN edge.
    std::uint32_t mag =
        value < 0 ? static_cast<std::uint32_t>(0) - static_cast<std::uint32_t>(value)
                  : static_cast<std::uint32_t>(value);
    char reversed[12];
    std::int32_t n = 0;
    do {
        reversed[n++] = static_cast<char>('0' + (mag % 10));
        mag /= 10;
    } while (mag > 0);

    if (value < 0) {
        out.bytes[out.count++] = '-';
    }
    while (n > 0) {
        out.bytes[out.count++] = reversed[--n];
    }
    return out;
}

NumberText format_decimal_padded(std::int32_t value, std::int32_t min_width, char pad) {
    NumberText out = format_decimal(value);
    while (out.count < min_width) {
        for (std::int32_t i = out.count; i > 0; --i) {
            out.bytes[i] = out.bytes[i - 1];
        }
        out.bytes[0] = pad;
        ++out.count;
    }
    return out;
}

}  // namespace sh::text
