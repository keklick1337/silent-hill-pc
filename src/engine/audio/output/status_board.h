// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The output's status, written by the sink's thread and read by anyone. The counters are atomics so the
// device's thread never waits on a reader.

#include "engine/audio/output/output_device.h"

#include <atomic>
#include <mutex>

namespace sh::audio::output {

struct FrameCounters {
    std::atomic<std::uint64_t> mixed{0};
    std::atomic<std::uint64_t> written{0};
    std::atomic<std::uint64_t> played{0};
    std::atomic<std::uint64_t> missing{0};
};

// What a sink opened.
struct OpenedDevice {
    OutputSink sink = OutputSink::Auto;
    OutputSharing sharing = OutputSharing::Shared;
    SampleFormat format = SampleFormat::S16;
    std::uint32_t device_rate = kMixRate;
    bool bit_perfect = false;
    std::string name;
};

class StatusBoard {
public:
    FrameCounters& counters() { return counters_; }

    // A new attempt to open the output: everything is forgotten.
    void reset();
    void set_state(OutputState state);
    // The sink failed, at opening or while it played.
    void fail(OutputResult result, std::int64_t native_error, std::string note);
    // A sink failed and another one is tried in its place.
    void fell_back(OutputResult reason, std::string note);
    void opened(const OpenedDevice& device);
    OutputStatus snapshot() const;

private:
    mutable std::mutex mutex_;
    OutputStatus status_;
    FrameCounters counters_;
};

}  // namespace sh::audio::output
