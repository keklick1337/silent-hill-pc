// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/audio/output/output_device.h"

#include "engine/audio/output/device_sink.h"

#include <algorithm>
#include <mutex>
#include <optional>
#include <utility>

namespace sh::audio::output {

namespace {

constexpr std::uint32_t kLatencyMs[2] = {3, 500};
constexpr std::uint32_t kQueueFrames[2] = {2048, 65536};

// The settings with every default filled in, or nothing when they contradict themselves.
std::optional<OutputSettings> resolved(OutputSettings settings) {
    if (settings.sink == OutputSink::VBlank) return std::nullopt;
    if (settings.device_rate == 0) settings.device_rate = kMixRate;
    if (rate_multiple(settings.device_rate) == 0) return std::nullopt;
    if (settings.latency_ms == 0) settings.latency_ms = OutputSettings{}.latency_ms;
    settings.latency_ms = std::clamp(settings.latency_ms, kLatencyMs[0], kLatencyMs[1]);
    if (settings.queue_frames == 0) settings.queue_frames = OutputSettings{}.queue_frames;
    settings.queue_frames = std::clamp(settings.queue_frames, kQueueFrames[0], kQueueFrames[1]);
    if (settings.bit_perfect) {
        // An untouched stream needs the device to itself, at the mix's own rate.
        if (settings.sink == OutputSink::Sdl || settings.sharing == OutputSharing::Shared ||
            settings.device_rate != kMixRate) {
            return std::nullopt;
        }
        settings.sharing = OutputSharing::Exclusive;
    }
    return settings;
}

}  // namespace

struct OutputDevice::Impl {
    explicit Impl(SinkProvider sinks) : provider(std::move(sinks)) {}
    ~Impl() { close(); }

    OutputResult open(const OutputSettings& asked, MixSource mix) {
        if (!mix) return OutputResult::InvalidSettings;
        std::lock_guard lock(mutex);
        if (sink) return OutputResult::AlreadyOpen;
        board.reset();
        const std::optional<OutputSettings> settings = resolved(asked);
        if (!settings) {
            board.fail(OutputResult::InvalidSettings, 0, "the sound output settings contradict each other");
            return OutputResult::InvalidSettings;
        }
        source = std::move(mix);
        const OutputResult result = open_first_that_plays(*settings);
        if (!playing(result)) source = nullptr;
        return result;
    }

    void close() {
        std::lock_guard lock(mutex);
        if (!sink) return;
        board.set_state(OutputState::Closing);
        sink->close();
        sink.reset();
        source = nullptr;
        board.set_state(OutputState::Closed);
    }

    void vblank(int blanks_per_second) {
        std::lock_guard lock(mutex);
        if (sink) sink->vblank(blanks_per_second);
    }

    OutputResult open_first_that_plays(const OutputSettings& settings) {
        if (provider.virtual_time && provider.virtual_time()) {
            return finished(try_sink(make(OutputSink::VBlank), settings, OutputSharing::Shared));
        }

        OutputResult result = OutputResult::SinkError;
        std::unique_ptr<DeviceSink> own = settings.sink == OutputSink::Sdl ? nullptr : make(OutputSink::Wasapi);
        const bool platform_has_own = own != nullptr;
        if (own) {
            OutputSharing sharing = settings.sharing;
            if (sharing == OutputSharing::Auto) {
                sharing = settings.bit_perfect ? OutputSharing::Exclusive : OutputSharing::Shared;
            }
            result = try_sink(std::move(own), settings, sharing);
            if (result == OutputResult::Ok) return finished(result);
        }

        // SDL is the first choice where it was asked for and where the platform has nothing of its own; after a
        // failed first choice it is the fallback, unless only an untouched stream will do.
        const bool sdl_first = settings.sink == OutputSink::Sdl || (settings.sink == OutputSink::Auto && !platform_has_own);
        if (!sdl_first && settings.bit_perfect) return result;
        if (!sdl_first) board.fell_back(result, "the platform's own sound output did not open; playing through SDL");
        return finished(try_sink(make(OutputSink::Sdl), settings, OutputSharing::Shared));
    }

    std::unique_ptr<DeviceSink> make(OutputSink kind) { return provider.make ? provider.make(kind) : nullptr; }

    OutputResult try_sink(std::unique_ptr<DeviceSink> candidate, const OutputSettings& settings, OutputSharing sharing) {
        if (!candidate) return OutputResult::SinkError;
        board.set_state(OutputState::Opening);
        SinkRequest request;
        request.sharing = sharing;
        request.device_rate = settings.device_rate;
        request.latency_ms = settings.latency_ms;
        request.queue_frames = settings.queue_frames;
        request.source = source;
        request.board = &board;
        const OutputResult result = candidate->open(request);
        if (result == OutputResult::Ok) sink = std::move(candidate);
        return result;
    }

    OutputResult finished(OutputResult result) const {
        if (result != OutputResult::Ok) return result;
        return board.snapshot().fallbacks != 0 ? OutputResult::OkFallback : OutputResult::Ok;
    }

    SinkProvider provider;
    std::mutex mutex;
    StatusBoard board;
    MixSource source;
    std::unique_ptr<DeviceSink> sink;
};

OutputDevice::OutputDevice(SinkProvider provider) : impl_(std::make_unique<Impl>(std::move(provider))) {}
OutputDevice::~OutputDevice() = default;

OutputResult OutputDevice::open(const OutputSettings& settings, MixSource source) {
    return impl_->open(settings, std::move(source));
}
void OutputDevice::close() { impl_->close(); }
OutputStatus OutputDevice::status() const { return impl_->board.snapshot(); }
void OutputDevice::vblank(int blanks_per_second) { impl_->vblank(blanks_per_second); }

}  // namespace sh::audio::output
