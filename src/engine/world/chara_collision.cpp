#include "engine/world/chara_collision.h"

#include "engine/world/fixed_math.h"

namespace sh::world {

std::vector<std::size_t> collidable_characters(std::span<const CollisionCandidate> candidates,
                                               const CollisionCandidate* excluded, int excluded_index,
                                               bool include_player, const CollisionStates& states) {
    std::vector<std::size_t> found;
    if (excluded != nullptr) {
        if (excluded->phased || !excluded->present || excluded->state == states.ignore ||
            (excluded->state == states.player && include_player)) {
            return found;
        }
    }
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const CollisionCandidate& c = candidates[i];
        if (!c.present || c.phased || c.state == states.ignore) {
            continue;
        }
        if (c.state == states.player && include_player) {
            continue;
        }
        if (static_cast<int>(i) == excluded_index) {
            continue;
        }
        if (include_player && excluded != nullptr && excluded->state == states.low && c.state < excluded->state) {
            continue;
        }
        found.push_back(i);
    }
    return found;
}

std::int32_t heard_loudness(std::int32_t scale, std::int32_t loudness, std::int32_t distance, std::int32_t offset_x,
                            std::int32_t offset_z) {
    if (distance == 0) {
        // Measured in Q.6 so the squares stay in range.
        distance = static_cast<std::int32_t>(static_cast<std::uint32_t>(planar_length(offset_x >> 6, offset_z >> 6)) << 6);
    }
    const std::int32_t heard = wrap_mul(scale, loudness) >> 12;
    const std::int32_t result = heard - (distance >> 8);
    return result < 0 ? 0 : result;
}

void scale_columns(std::array<std::array<std::int16_t, 3>, 3>& m, std::int16_t x, std::int16_t y, std::int16_t z) {
    const std::int32_t factors[3] = {x, y, z};
    for (int column = 0; column < 3; ++column) {
        if (factors[column] == 4096) {
            continue;
        }
        for (int row = 0; row < 3; ++row) {
            m[row][column] = static_cast<std::int16_t>((static_cast<std::int64_t>(factors[column]) * m[row][column]) >> 12);
        }
    }
}

}  // namespace sh::world
