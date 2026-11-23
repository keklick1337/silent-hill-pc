// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/audio/output/status_board.h"

#include <utility>

namespace sh::audio::output {

void StatusBoard::reset() {
    std::lock_guard lock(mutex_);
    status_ = OutputStatus{};
    counters_.mixed.store(0, std::memory_order_relaxed);
    counters_.written.store(0, std::memory_order_relaxed);
    counters_.played.store(0, std::memory_order_relaxed);
    counters_.missing.store(0, std::memory_order_relaxed);
}

void StatusBoard::set_state(OutputState state) {
    std::lock_guard lock(mutex_);
    status_.state = state;
}

void StatusBoard::fail(OutputResult result, std::int64_t native_error, std::string note) {
    std::lock_guard lock(mutex_);
    status_.state = OutputState::Failed;
    status_.result = result;
    status_.native_error = native_error;
    status_.note = std::move(note);
}

void StatusBoard::fell_back(OutputResult reason, std::string note) {
    std::lock_guard lock(mutex_);
    ++status_.fallbacks;
    status_.fallback_reason = reason;
    status_.result = OutputResult::OkFallback;
    status_.note = std::move(note);
}

void StatusBoard::opened(const OpenedDevice& device) {
    std::lock_guard lock(mutex_);
    status_.sink = device.sink;
    status_.sharing = device.sharing;
    status_.format = device.format;
    status_.device_rate = device.device_rate;
    status_.bit_perfect = device.bit_perfect;
    status_.device_name = device.name;
    // After a fallback the note and the error number keep saying why the first choice failed.
    if (status_.fallbacks == 0) {
        status_.result = OutputResult::Ok;
        status_.native_error = 0;
        status_.note.clear();
    }
}

OutputStatus StatusBoard::snapshot() const {
    OutputStatus out;
    {
        std::lock_guard lock(mutex_);
        out = status_;
    }
    out.mix_frames = counters_.mixed.load(std::memory_order_relaxed);
    out.device_frames_written = counters_.written.load(std::memory_order_relaxed);
    out.device_frames_played = counters_.played.load(std::memory_order_relaxed);
    out.missing_frames = counters_.missing.load(std::memory_order_relaxed);
    return out;
}

}  // namespace sh::audio::output
