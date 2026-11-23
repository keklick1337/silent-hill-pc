// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/audio/output/vblank_sink.h"

#include "engine/audio/output/mix_feed.h"

#include <algorithm>
#include <utility>

namespace sh::audio::output {

VBlankSink::VBlankSink(std::filesystem::path capture) : capture_path_(std::move(capture)) {}

OutputResult VBlankSink::open(const SinkRequest& request) {
    source_ = request.source;
    board_ = request.board;
    carried_ = 0;
    chunk_.assign(2u * MixFeed::kRenderChunk, 0);
    if (!capture_path_.empty()) capture_.open(capture_path_);

    OpenedDevice device;
    device.sink = OutputSink::VBlank;
    device.name = "replay clock (silent)";
    board_->opened(device);
    board_->set_state(OutputState::Playing);
    return OutputResult::Ok;
}

void VBlankSink::close() {
    capture_.close();
    source_ = nullptr;
}

void VBlankSink::vblank(int blanks_per_second) {
    if (blanks_per_second <= 0 || board_ == nullptr) return;
    // 44100 / 60 is exact, 44100 / 50 is exact, but any other rate leaves a remainder: it is carried to the
    // next blank, so a second of blanks is always a second of sound.
    const std::uint32_t rate = static_cast<std::uint32_t>(blanks_per_second);
    const std::uint64_t due = static_cast<std::uint64_t>(kMixRate) + carried_;
    std::uint32_t frames = static_cast<std::uint32_t>(due / rate);
    carried_ = static_cast<std::uint32_t>(due % rate);

    FrameCounters& counters = board_->counters();
    while (frames > 0) {
        const std::uint32_t count = std::min(frames, MixFeed::kRenderChunk);
        if (source_) {
            source_(chunk_.data(), count);
            capture_.append(chunk_.data(), count);
        }
        counters.mixed.fetch_add(count, std::memory_order_relaxed);
        counters.written.fetch_add(count, std::memory_order_relaxed);
        counters.played.fetch_add(count, std::memory_order_relaxed);
        frames -= count;
    }
    capture_.seal();
}

}  // namespace sh::audio::output
