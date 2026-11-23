#include "engine/world/fixed_trig.h"

#include "engine/world/fixed_math.h"
#include "engine/world/trig_table.inc"

#include <cmath>

namespace sh::world {

namespace {

// Same double evaluation the game's runtime performs, truncated to Q.12 like a C cast.
constexpr double kPi = 3.14159265358979323846;

double to_radians(std::int32_t angle_q12) {
    return (static_cast<double>(angle_q12) / 4096.0) * 2.0 * kPi;
}

// Unsigned division; both operands are non-negative here, so C truncation is floor.
std::int32_t ratio_scaled(std::int32_t small, std::int32_t large) {
    if ((static_cast<std::uint32_t>(small) & 0x7fe00000U) == 0) {
        return (small << 10) / large;
    }
    return small / (large >> 10);
}

std::int32_t max3(std::int32_t a, std::int32_t b, std::int32_t c) {
    return a >= b ? (a >= c ? a : c) : (b >= c ? b : c);
}

}  // namespace

std::int32_t fixed_sin(std::int32_t angle_q12) {
    return static_cast<std::int32_t>(std::sin(to_radians(angle_q12)) * 4096.0);
}

std::int32_t fixed_cos(std::int32_t angle_q12) {
    return static_cast<std::int32_t>(std::cos(to_radians(angle_q12)) * 4096.0);
}

std::int32_t fixed_sqrt(std::int32_t value) { return game_sqrt(value); }

std::int32_t fixed_atan2(std::int32_t y, std::int32_t x) {
    if (x == 0 && y == 0) {
        return 0;
    }
    const bool x_negative = x < 0;
    const bool y_negative = y < 0;
    if (x_negative) {
        x = -x;
    }
    if (y_negative) {
        y = -y;
    }

    std::int32_t angle;
    if (y < x) {
        angle = detail::kArcTangentTable[ratio_scaled(y, x)];
    } else {
        angle = 1024 - detail::kArcTangentTable[ratio_scaled(x, y)];
    }
    if (x_negative) {
        angle = 2048 - angle;
    }
    if (y_negative) {
        angle = -angle;
    }
    return angle;
}

std::int32_t fixed_length3(std::int32_t x, std::int32_t y, std::int32_t z) {
    const std::int32_t mag = max3(x < 0 ? -x : x, y < 0 ? -y : y, z < 0 ? -z : z);
    std::int32_t shift = 0;
    if (mag > (1 << 18) - 1) {
        shift = mag > (1 << 22) - 1 ? 12 : 8;
    } else if (mag >= (1 << 14)) {
        shift = 4;
    }
    x >>= shift;
    y >>= shift;
    z >>= shift;
    const std::int32_t squares =
        wrap_add(wrap_add(wrap_mul(x, x), wrap_mul(y, y)), wrap_mul(z, z));
    return game_sqrt(squares) << shift;
}

}  // namespace sh::world
