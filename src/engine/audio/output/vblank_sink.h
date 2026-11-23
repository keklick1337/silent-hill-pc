// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The sink of a replay. A device pulls the mix at the speed of the wall clock, so what
// the game reads back from the sound side (a voice's key status, a line having drained) would depend on the
// machine. Here the mix is rendered by the replay's own clock instead: each vertical blank renders that
// blank's share of a second, and nothing is heard. The frames can be kept in a WAV file (MixCapture).

#include "engine/audio/output/device_sink.h"
#include "engine/audio/output/mix_capture.h"

#include <filesystem>
#include <vector>

namespace sh::audio::output {

class VBlankSink final : public DeviceSink {
public:
    // `capture`: the WAV file that receives every rendered frame; empty for none.
    explicit VBlankSink(std::filesystem::path capture = {});

    OutputResult open(const SinkRequest& request) override;
    void close() override;
    void vblank(int blanks_per_second) override;

private:
    std::filesystem::path capture_path_;
    MixCapture capture_;
    MixSource source_;
    StatusBoard* board_ = nullptr;
    // What the division of the mix rate by the blank rate left over, in units of 1 / blanks_per_second frame.
    std::uint32_t carried_ = 0;
    std::vector<std::int16_t> chunk_;
};

}  // namespace sh::audio::output
