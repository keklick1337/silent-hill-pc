#include "engine/gameflow/title_fog.h"

#include "engine/world/fixed_trig.h"

namespace sh::gameflow {

namespace {

constexpr int kScattersAtInit = 30;
constexpr int kFramesPerScatter = 5;
constexpr int kHotCells = 16;
constexpr int kColdCells = 9;
constexpr std::int32_t kSeedBase = -122;  // stored as a byte: 134
constexpr std::int32_t kSeedSwing = 10;

}  // namespace

void TitleFog::init(Random& rng) {
    grid_.fill(0);
    ready_ = true;
    for (int i = 0; i < kScattersAtInit; ++i) {
        scatter(rng);
    }
}

bool TitleFog::frame(Random& rng) {
    if (!ready_) {
        ++frames_;
        return false;
    }
    if (frames_ % kFramesPerScatter == 0) {
        scatter(rng);
    }
    ++frames_;
    return true;
}

void TitleFog::seed(Random& rng) {
    std::uint8_t* row = &grid_[1 + kSide * kSide];
    swing_angle_ += rng.rand16() % 8 + 4;
    const std::int32_t base = ((sh::world::fixed_sin(swing_angle_) * kSeedSwing) >> 12) + kSeedBase;
    for (int i = 0; i < kSide; ++i) {
        row[i] = static_cast<std::uint8_t>(base);
    }
    for (int i = 0; i < kHotCells; ++i) {
        row[rng.rand16() % kSide] = 0xFF;
    }
    for (int i = 0; i < kColdCells; ++i) {
        row[rng.rand16() % kSide] = 0;
    }
}

void TitleFog::scatter(Random& rng) {
    seed(rng);
    // In place, row by row from the top: a cell sees its left neighbour already updated and the row
    // below not yet updated; the first cell reads the guard cell.
    for (int cell = 0; cell < kSide * kSide; ++cell) {
        const std::size_t c = static_cast<std::size_t>(cell + 1);
        std::int32_t v = grid_[c + kSide] + grid_[c - 1] + grid_[c] + grid_[c + 1];
        v = (v >> 2) - 1;
        grid_[c] = v <= 0 ? 0 : static_cast<std::uint8_t>(v);
    }
}

}  // namespace sh::gameflow
