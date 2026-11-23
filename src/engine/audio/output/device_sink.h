// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What a sink is: one way of getting the mix to a device. OutputDevice opens one at a time and gives it the
// mix source and the board it reports on; the sink owns its thread or callback and whatever buffering the device
// needs.

#include "engine/audio/output/output_device.h"
#include "engine/audio/output/status_board.h"

namespace sh::audio::output {

struct SinkRequest {
    // Shared or Exclusive, never Auto.
    OutputSharing sharing = OutputSharing::Shared;
    std::uint32_t device_rate = kMixRate;
    std::uint32_t latency_ms = 20;
    std::uint32_t queue_frames = 8192;
    MixSource source;
    // Outlives the sink.
    StatusBoard* board = nullptr;
};

class DeviceSink {
public:
    virtual ~DeviceSink() = default;
    // Ok once the device plays. On a failure the sink has reported it on the board and holds nothing open.
    virtual OutputResult open(const SinkRequest& request) = 0;
    // Stops the device; no call of the source is running or will start once this returns.
    virtual void close() = 0;
    // A replay's clock ticked. Only the VBlank sink renders on it.
    virtual void vblank(int /*blanks_per_second*/) {}
};

}  // namespace sh::audio::output
