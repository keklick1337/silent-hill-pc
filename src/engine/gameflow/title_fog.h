#pragma once

// The drifting fog of the title screen: a heat grid of 21 x 21
// cells plus a seed row under it. Every scatter re-seeds the bottom row (a slowly swinging base value
// with random hot and cold cells) and lets every cell take the average of itself, its left and right
// neighbours and the cell below, minus one, so the heat rises and fades. The host draws the lower half
// of the grid as shaded quads through a palette of the game.
//
// Bit-exactness contract: with the same random numbers the grid equals the original's byte for byte,
// including the read of one cell before the first (a guard cell, kept 0).

#include <array>
#include <cstdint>

namespace sh::gameflow {

class TitleFog {
public:
    static constexpr int kSide = 21;
    static constexpr int kCells = kSide * kSide + kSide;  // the grid and its seed row
    static constexpr int kFirstDrawnCell = 10 * kSide;    // cells above are drawn black

    // The game's 16-bit random number (0..32767).
    class Random {
    public:
        virtual ~Random() = default;
        virtual std::int32_t rand16() = 0;
    };

    // Clears the grid and lets the fog build up (30 scatters).
    void init(Random& rng);
    // One title frame: a scatter every 5th frame. Returns false before init (nothing to draw).
    bool frame(Random& rng);

    bool ready() const { return ready_; }
    std::uint8_t heat(int cell) const { return grid_[static_cast<std::size_t>(cell + 1)]; }
    // Palette index of a cell as drawn, or -1 for black.
    int shade(int cell) const { return cell < kFirstDrawnCell ? -1 : heat(cell); }

private:
    void seed(Random& rng);
    void scatter(Random& rng);

    // grid_[0] is the guard cell before the grid.
    std::array<std::uint8_t, kCells + 1> grid_{};
    bool ready_ = false;
    std::int32_t frames_ = 0;
    std::int32_t swing_angle_ = 0;  // Q12 angle of the seed row's base value
};

}  // namespace sh::gameflow
