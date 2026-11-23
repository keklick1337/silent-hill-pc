#include "engine/world/line_of_sight.h"

namespace sh::world {

std::int32_t normalize_heading(std::int32_t angle) {
    std::int32_t norm = angle & 4095;
    // The fold keeps a half turn at +2048 and turns (2048, 4095] negative.
    if (norm > 2048) {
        norm -= 4096;
    }
    return norm;
}

std::int32_t planar_distance_q12(std::int32_t dx, std::int32_t dz) {
    // Squared in Q.6 so a whole map of separation still fits; the root is the game's
    // table-driven one, exact for small squares.
    const std::int32_t x6 = dx >> 6;
    const std::int32_t z6 = dz >> 6;
    return game_sqrt(wrap_add(wrap_mul(x6, x6), wrap_mul(z6, z6))) << 6;
}

Vec3 heading_offset(std::int32_t dist, std::int32_t heading) {
    // The products wrap in 32 bits before the shift, as the game's macro does.
    return Vec3{wrap_mul(dist, fixed_sin(heading)) >> 12, 0, wrap_mul(dist, fixed_cos(heading)) >> 12};
}

}  // namespace sh::world
