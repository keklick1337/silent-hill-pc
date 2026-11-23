// SPDX-License-Identifier: GPL-3.0-or-later
// Monotonic stopwatch on std::chrono::steady_clock.
#pragma once

#include <chrono>

namespace sh::timing {

class HpTimer
{
public:
    HpTimer() : start_(Clock::now()) {}

    // Seconds since construction or the last reset.
    double elapsed() const { return std::chrono::duration<double>(Clock::now() - start_).count(); }

    // Seconds since the last lap (or construction); the next lap starts now.
    double lap()
    {
        const Clock::time_point now = Clock::now();
        const double seconds = std::chrono::duration<double>(now - start_).count();
        start_ = now;
        return seconds;
    }

    void reset() { start_ = Clock::now(); }

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point start_;
};

} // namespace sh::timing
