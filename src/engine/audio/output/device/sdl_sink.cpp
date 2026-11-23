// SPDX-License-Identifier: GPL-3.0-or-later
// The mix through SDL's audio: the system's default playback device, opened with a stream that takes exactly
// the mix's format (16-bit stereo at 44.1 kHz). SDL converts to whatever the device runs at, so this sink plays
// on every machine SDL has a driver for; it does not offer the multiples of the mix rate.

#include "engine/audio/output/device/platform_sinks.h"

#include "engine/audio/output/mix_feed.h"
#include "engine/log/sh_log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <optional>
#include <string>
#include <vector>

namespace sh::audio::output {

namespace {

constexpr std::uint32_t kStreamFrameBytes = 4;
// The device period SDL is asked for: the power of two that holds the latency, within these bounds.
constexpr std::uint32_t kPeriodFrames[2] = {256, 4096};
// The mix is rendered at least this far ahead of the device.
constexpr std::uint32_t kLeastLeadFrames = 256;

std::uint32_t period_for(std::uint32_t latency_ms) {
    const std::uint32_t wanted = std::clamp(static_cast<std::uint32_t>(std::uint64_t{kMixRate} * latency_ms / 1000),
                                            kPeriodFrames[0], kPeriodFrames[1]);
    std::uint32_t period = kPeriodFrames[0];
    while (period < wanted) period *= 2;
    return period;
}

class SdlSink final : public DeviceSink {
public:
    ~SdlSink() override { close(); }

    OutputResult open(const SinkRequest& request) override {
        board_ = request.board;
        if (request.device_rate != kMixRate) {
            return failed(OutputResult::UnsupportedFormat, "SDL plays the mix at 44.1 kHz only");
        }
        if (!SDL_WasInit(SDL_INIT_AUDIO)) {
            if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return failed(OutputResult::SinkError, SDL_GetError());
            started_audio_ = true;
        }

        const std::uint32_t asked_period = period_for(request.latency_ms);
        SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, std::to_string(asked_period).c_str());
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_S16;
        spec.channels = 2;
        spec.freq = static_cast<int>(kMixRate);
        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &SdlSink::on_stream_wants, this);
        if (stream_ == nullptr) return failed(OutputResult::NoDevice, SDL_GetError());

        const SDL_AudioDeviceID device = SDL_GetAudioStreamDevice(stream_);
        SDL_AudioSpec device_spec{};
        int device_period = 0;
        if (!SDL_GetAudioDeviceFormat(device, &device_spec, &device_period) || device_period <= 0) {
            device_period = static_cast<int>(asked_period);
        }
        const std::uint32_t period = static_cast<std::uint32_t>(device_period);

        // The queue holds two device periods at least; the lead is one period, and no more than half the queue.
        const std::uint32_t queue_frames = std::max(request.queue_frames, period * 2 + 1);
        lead_frames_ = std::min(queue_frames / 2, std::max(period, kLeastLeadFrames));
        feed_.emplace(request.source, queue_frames, board_->counters());
        writer_.emplace(*feed_, SampleFormat::S16, 1u);
        closing_.store(false, std::memory_order_release);

        OpenedDevice opened;
        opened.sink = OutputSink::Sdl;
        opened.name = "SDL default output";
        board_->opened(opened);
        board_->set_state(OutputState::Priming);
        feed_->render_until(lead_frames_);
        board_->set_state(OutputState::Playing);
        SH_LOG_INFO(AUDIO, "SDL audio: device '%s' at %d Hz, %d frames per period (asked %u), lead %u frames",
                    SDL_GetAudioDeviceName(device), device_spec.freq, device_period, asked_period, lead_frames_);
        SDL_ResumeAudioStreamDevice(stream_);
        return OutputResult::Ok;
    }

    void close() override {
        if (stream_ != nullptr) {
            closing_.store(true, std::memory_order_release);
            SDL_PauseAudioStreamDevice(stream_);
            // Destroying the stream closes the device it was opened with and waits for a running callback.
            SDL_DestroyAudioStream(stream_);
            stream_ = nullptr;
        }
        writer_.reset();
        feed_.reset();
        release_audio();
    }

private:
    OutputResult failed(OutputResult result, const char* note) {
        board_->fail(result, 0, note != nullptr ? note : "");
        release_audio();
        return result;
    }

    void release_audio() {
        if (!started_audio_) return;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        started_audio_ = false;
    }

    // SDL calls this on its audio thread when the stream needs at least `needed` more bytes.
    static void SDLCALL on_stream_wants(void* self, SDL_AudioStream* stream, int needed, int /*total*/) {
        if (needed <= 0) return;
        static_cast<SdlSink*>(self)->supply(stream, static_cast<std::uint32_t>(needed));
    }

    void supply(SDL_AudioStream* stream, std::uint32_t needed_bytes) {
        const std::uint32_t frames = (needed_bytes + kStreamFrameBytes - 1) / kStreamFrameBytes;
        bytes_.resize(std::max<std::size_t>(bytes_.size(), std::size_t{frames} * kStreamFrameBytes));
        if (closing_.load(std::memory_order_acquire)) {
            std::fill_n(bytes_.begin(), std::size_t{frames} * kStreamFrameBytes, std::uint8_t{0});
        } else {
            feed_->render_until(feed_->taken() + writer_->mix_frames_for(frames + lead_frames_));
            writer_->write(bytes_.data(), frames);
            board_->counters().written.fetch_add(frames, std::memory_order_relaxed);
        }
        SDL_PutAudioStreamData(stream, bytes_.data(), static_cast<int>(frames * kStreamFrameBytes));
    }

    SDL_AudioStream* stream_ = nullptr;
    bool started_audio_ = false;
    std::atomic<bool> closing_{false};
    StatusBoard* board_ = nullptr;
    std::uint32_t lead_frames_ = 0;
    std::optional<MixFeed> feed_;
    std::optional<DeviceWriter> writer_;
    std::vector<std::uint8_t> bytes_;
};

}  // namespace

std::unique_ptr<DeviceSink> make_sdl_sink() { return std::make_unique<SdlSink>(); }

}  // namespace sh::audio::output
