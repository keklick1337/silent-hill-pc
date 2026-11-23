// SPDX-License-Identifier: GPL-3.0-or-later
//
// Frame pacing figures of the frame-rate counter and the profiler (fps_counter.h).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sh::hud {

// Frame times of the last frames, for the "1% low" figure: the average of the slowest hundredth.
class FrameTimes {
public:
    static constexpr std::size_t kCapacity = 1024;

    void add(float milliseconds) noexcept;
    std::size_t count() const noexcept { return count_; }
    // Average of the slowest count()/100 frames (at least one), in milliseconds; 0 without frames.
    double slowest_percent() const;

private:
    std::array<float, kCapacity> times_{};
    std::size_t count_ = 0;
    std::size_t next_ = 0;
};

// Counts frames over half-second windows.
class FrameWindow {
public:
    struct Closed {
        int frames = 0;
        std::uint64_t ticks = 0;
        std::uint64_t frequency = 1;

        double seconds() const { return static_cast<double>(ticks) / static_cast<double>(frequency); }
        // Frames a second, rounded to the nearest whole.
        int rate() const {
            return static_cast<int>(static_cast<double>(frames) * static_cast<double>(frequency) /
                                        static_cast<double>(ticks) + 0.5);
        }
    };

    void reset() noexcept { started_ = false; }
    // Counts one frame at clock tick `now` (`frequency` ticks a second). True when that frame closes a window:
    // `closed` then tells its length, and the next window starts.
    bool frame(std::uint64_t now, std::uint64_t frequency, Closed& closed) noexcept;

private:
    bool started_ = false;
    std::uint64_t start_ = 0;
    int frames_ = 0;
};

}  // namespace sh::hud
