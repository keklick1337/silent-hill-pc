// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A bounded first-in first-out store of stereo frames: the mix that was rendered and not yet given to the device.
// One thread uses it (the device's own), so it needs no locking.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sh::audio::output {

struct StereoFrame {
    std::int16_t left = 0;
    std::int16_t right = 0;

    bool operator==(const StereoFrame&) const = default;
};

class FrameQueue {
public:
    // Holds up to `capacity` frames (at least two).
    explicit FrameQueue(std::size_t capacity) : slots_(capacity < 2 ? 2 : capacity) {}

    std::size_t capacity() const { return slots_.size(); }
    std::size_t size() const { return held_; }
    std::size_t space() const { return slots_.size() - held_; }
    bool empty() const { return held_ == 0; }

    // Appends interleaved left/right samples; returns how many frames fitted.
    std::size_t push(const std::int16_t* interleaved, std::size_t frames) {
        const std::size_t taken = frames < space() ? frames : space();
        std::size_t at = (head_ + held_) % slots_.size();
        for (std::size_t i = 0; i < taken; ++i) {
            slots_[at] = StereoFrame{interleaved[2 * i], interleaved[2 * i + 1]};
            at = at + 1 == slots_.size() ? 0 : at + 1;
        }
        held_ += taken;
        return taken;
    }

    // Removes the oldest frame into `frame`; false, and `frame` untouched, when there is none.
    bool pop(StereoFrame& frame) {
        if (held_ == 0) return false;
        frame = slots_[head_];
        head_ = head_ + 1 == slots_.size() ? 0 : head_ + 1;
        --held_;
        return true;
    }

    void clear() {
        head_ = 0;
        held_ = 0;
    }

private:
    std::vector<StereoFrame> slots_;
    std::size_t head_ = 0;
    std::size_t held_ = 0;
};

}  // namespace sh::audio::output
