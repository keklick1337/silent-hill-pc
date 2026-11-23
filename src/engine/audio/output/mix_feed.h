// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The path of the mix from the engine to a device's buffer. MixFeed renders the mix a little ahead of the
// device into a queue, so that a device asking for an odd amount is served from frames that already exist;
// DeviceWriter takes frames from it and writes them in the device's format, each one as many times as the
// device's rate is a multiple of the mix rate.

#include "engine/audio/output/device_format.h"
#include "engine/audio/output/output_device.h"
#include "engine/audio/output/status_board.h"

#include <cstdint>
#include <vector>

namespace sh::audio::output {

class MixFeed {
public:
    // The most the source is asked for in one call.
    static constexpr std::uint32_t kRenderChunk = 1024;

    MixFeed(MixSource source, std::uint32_t queue_frames, FrameCounters& counters);

    // Renders until `total` frames were rendered since the feed began, or the queue is full. Frames the source
    // does not deliver become silence and are counted as missing.
    void render_until(std::uint64_t total);
    // The next frame for the device. An empty queue gives silence, counted as missing.
    StereoFrame take();

    std::uint64_t rendered() const { return rendered_; }
    std::uint64_t taken() const { return taken_; }
    std::size_t queued() const { return queue_.size(); }

private:
    MixSource source_;
    FrameCounters& counters_;
    FrameQueue queue_;
    std::vector<std::int16_t> chunk_;
    std::uint64_t rendered_ = 0;
    std::uint64_t taken_ = 0;
};

class DeviceWriter {
public:
    // `repeat`: device frames per mix frame (rate_multiple of the device's rate), at least 1.
    DeviceWriter(MixFeed& feed, SampleFormat format, std::uint32_t repeat);

    // Mix frames the feed must still hand out to write `device_frames`.
    std::uint64_t mix_frames_for(std::uint32_t device_frames) const;
    // Writes `device_frames` frames (frame_bytes(format) each) at `out`.
    void write(std::uint8_t* out, std::uint32_t device_frames);

private:
    MixFeed& feed_;
    SampleFormat format_;
    std::uint32_t repeat_;
    // Copies of `current_` the device has not been given yet.
    std::uint32_t pending_ = 0;
    StereoFrame current_;
};

}  // namespace sh::audio::output
