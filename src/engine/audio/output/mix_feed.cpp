// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/audio/output/mix_feed.h"

#include <algorithm>
#include <utility>

namespace sh::audio::output {

MixFeed::MixFeed(MixSource source, std::uint32_t queue_frames, FrameCounters& counters)
    : source_(std::move(source)),
      counters_(counters),
      queue_(queue_frames),
      chunk_(2u * std::min(kRenderChunk, queue_frames)) {}

void MixFeed::render_until(std::uint64_t total) {
    const std::uint64_t chunk_frames = chunk_.size() / 2;
    while (rendered_ < total) {
        const std::uint32_t wanted = static_cast<std::uint32_t>(
            std::min({total - rendered_, chunk_frames, static_cast<std::uint64_t>(queue_.space())}));
        if (wanted == 0) break;
        const std::uint32_t got = std::min(source_ ? source_(chunk_.data(), wanted) : 0u, wanted);
        if (got < wanted) {
            std::fill(chunk_.begin() + 2 * got, chunk_.begin() + 2 * wanted, std::int16_t{0});
            counters_.missing.fetch_add(wanted - got, std::memory_order_relaxed);
        }
        queue_.push(chunk_.data(), wanted);
        rendered_ += wanted;
        counters_.mixed.fetch_add(wanted, std::memory_order_relaxed);
    }
}

StereoFrame MixFeed::take() {
    StereoFrame frame;
    if (!queue_.pop(frame)) counters_.missing.fetch_add(1, std::memory_order_relaxed);
    ++taken_;
    return frame;
}

DeviceWriter::DeviceWriter(MixFeed& feed, SampleFormat format, std::uint32_t repeat)
    : feed_(feed), format_(format), repeat_(std::max(repeat, 1u)) {}

std::uint64_t DeviceWriter::mix_frames_for(std::uint32_t device_frames) const {
    if (device_frames <= pending_) return 0;
    return (static_cast<std::uint64_t>(device_frames - pending_) + repeat_ - 1) / repeat_;
}

void DeviceWriter::write(std::uint8_t* out, std::uint32_t device_frames) {
    const std::uint32_t stride = frame_bytes(format_);
    for (std::uint32_t i = 0; i < device_frames; ++i, out += stride) {
        if (pending_ == 0) {
            current_ = feed_.take();
            pending_ = repeat_;
        }
        encode_frame(current_, format_, out);
        --pending_;
    }
}

}  // namespace sh::audio::output
