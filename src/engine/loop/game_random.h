// The game's random number generator: one linear congruential
// generator shared by everything that rolls dice in the game (enemy AI, effects, game over tips,
// title fog, Lua `sh.rng_*`). Demo playback and replays depend on its exact sequence, so the
// multiplier, increment and the bits each draw keeps are the original's.
#pragma once

#include <cstdint>

namespace sh::loop {

class GameRandom {
public:
    static constexpr std::uint32_t kMultiplier = 1664525u;
    static constexpr std::uint32_t kIncrement  = 1013904223u;

    std::uint32_t seed() const { return seed_; }
    void set_seed(std::uint32_t seed) { seed_ = seed; }

    // Advances the generator; the whole 32-bit state is the draw.
    std::uint32_t next32() {
        seed_ = seed_ * kMultiplier + kIncrement;
        return seed_;
    }
    // Top 15 bits: [0, 0x7FFF].
    std::int32_t next15() { return static_cast<std::int32_t>(next32() >> 17); }
    // Top 12 bits: [0, 0xFFF].
    std::uint16_t next12() { return static_cast<std::uint16_t>(next32() >> 20); }
    // A Q12 fraction [0, 0xFFF] mixed from a 15-bit draw (the player code's "random angle").
    std::int32_t next_q12_fraction() {
        const std::int32_t r = next15();
        return ((r * 2) ^ r) >> 3 & 0xFFF;
    }
    // Integer in [low, high] from a 15-bit draw, the remainder taken signed or unsigned
    // (the two spellings differ only when high - low + 1 is not positive).
    std::int32_t next_int(std::int32_t low, std::int32_t high) {
        return next15() % (high - low + 1) + low;
    }
    std::int32_t next_uint(std::int32_t low, std::int32_t high) {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(next15()) %
                                         static_cast<std::uint32_t>(high - low + 1)) + low;
    }

private:
    std::uint32_t seed_ = 0;
};

// The one generator of the running game.
GameRandom& game_random();

}  // namespace sh::loop
