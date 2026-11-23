// SPDX-License-Identifier: GPL-3.0-or-later
#include "features/hud/frame_stats.h"

#include <algorithm>
#include <functional>
#include <vector>

namespace sh::hud {

void FrameTimes::add(float milliseconds) noexcept {
    times_[next_] = milliseconds;
    next_ = (next_ + 1) % kCapacity;
    count_ = std::min(count_ + 1, kCapacity);
}

double FrameTimes::slowest_percent() const {
    if (count_ == 0) {
        return 0.0;
    }
    std::vector<float> sorted(times_.begin(), times_.begin() + static_cast<std::ptrdiff_t>(count_));
    const std::size_t worst = std::max<std::size_t>(1, count_ / 100);
    std::partial_sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(worst), sorted.end(),
                      std::greater<float>());
    double sum = 0.0;
    for (std::size_t i = 0; i < worst; ++i) {
        sum += sorted[i];
    }
    return sum / static_cast<double>(worst);
}

bool FrameWindow::frame(std::uint64_t now, std::uint64_t frequency, Closed& closed) noexcept {
    if (!started_) {
        started_ = true;
        start_ = now;
        frames_ = 0;
    }
    ++frames_;
    if (now - start_ < frequency / 2) {
        return false;
    }
    closed = {frames_, now - start_, frequency};
    start_ = now;
    frames_ = 0;
    return true;
}

}  // namespace sh::hud
