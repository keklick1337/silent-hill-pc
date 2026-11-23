#pragma once

// Character rules of the collision queries: which characters a
// move or a ray tests against, how loud the player is at a distance, and the bone scale of a body.
//
// Bit-exactness contract: the results equal the original's to the last bit.

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace sh::world {

// Collision states a character can be in, as the game numbers them.
struct CollisionStates {
    int ignore = 0;  // collides with nothing
    int player = 0;  // the player's own state: left out when the player is included separately
    int low = 0;     // a character in this state only meets characters in the same or a higher state
};

struct CollisionCandidate {
    bool present = false;  // a character occupies the slot
    bool phased = false;   // the console's NOCOLLIDE: collides with nobody, nobody with it
    int state = 0;
};

// Indices of the candidates a character collides with. `excluded` is the asking character (null: none),
// `excluded_index` its place in `candidates` (-1 when it is not one of them).
std::vector<std::size_t> collidable_characters(std::span<const CollisionCandidate> candidates,
                                               const CollisionCandidate* excluded, int excluded_index,
                                               bool include_player, const CollisionStates& states);

// How loud the player is at a distance (Q.12): scale x loudness, less 1/256 of the distance, not below 0.
// A zero distance is measured from the planar offset (Q.12).
std::int32_t heard_loudness(std::int32_t scale, std::int32_t loudness, std::int32_t distance, std::int32_t offset_x,
                            std::int32_t offset_z);

// Scales the columns of a 3x3 Q.12 rotation by (x, y, z); a factor of 1.0 leaves its column alone.
void scale_columns(std::array<std::array<std::int16_t, 3>, 3>& m, std::int16_t x, std::int16_t y, std::int16_t z);

}  // namespace sh::world
