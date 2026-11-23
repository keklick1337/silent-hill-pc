// SPDX-License-Identifier: GPL-3.0-or-later
// sh-audio-check: opens the engine's sound output the way the game does, plays a few seconds through it and
// reports what the device took. For checking a machine's sound output (or a sink under wine) without the game.
// It plays silence unless a tone is asked for.

#include "engine/audio/output/device/platform_sinks.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace {

namespace out = sh::audio::output;

const char* kUsage =
    "usage: sh-audio-check [options]\n"
    "  --sink auto|sdl|wasapi        the device interface (config.cfg: audio_backend)\n"
    "  --mode auto|shared|exclusive  how the device is shared (audio_mode)\n"
    "  --rate N                      the device's rate: 44100, 88200, 176400 or 352800 (audio_rate)\n"
    "  --bit-perfect                 only an exclusive device at 44100 Hz (audio_bit_perfect)\n"
    "  --seconds S                   how long to play (default 2)\n"
    "  --tone                        a quiet 440 Hz tone instead of silence\n"
    "  --wasapi-convert              (Windows, testing) let Windows convert the stream for a shared device,\n"
    "                                so the WASAPI sink plays where the game would fall back to SDL\n"
    "Exit code: 0 the device played and took the whole mix, 1 it did not, 2 bad arguments.\n";

const char* name_of(out::SampleFormat format) {
    switch (format) {
        case out::SampleFormat::S16: return "16-bit";
        case out::SampleFormat::S24: return "24-bit packed";
        case out::SampleFormat::S24In32: return "24-bit in 32";
        case out::SampleFormat::S32: return "32-bit";
        case out::SampleFormat::Unknown: break;
    }
    return "unknown";
}

void print(const char* when, const out::OutputStatus& status) {
    std::printf("%s: sink %s, %s, %s at %u Hz, device '%s'%s\n", when, out::name_of(status.sink),
                out::name_of(status.sharing), name_of(status.format), status.device_rate, status.device_name.c_str(),
                status.bit_perfect ? ", bit-perfect" : "");
    const unsigned long long native_error = static_cast<unsigned long long>(status.native_error) & 0xFFFFFFFFull;
    if (status.fallbacks != 0) {
        std::printf("  fell back: %s (the first choice: %s, platform error 0x%llx)\n", status.note.c_str(),
                    out::name_of(status.fallback_reason), native_error);
    } else if (!status.note.empty()) {
        std::printf("  note: %s (platform error 0x%llx)\n", status.note.c_str(), native_error);
    }
    std::printf("  mix frames %llu, device frames written %llu, played %llu, missing %llu\n",
                static_cast<unsigned long long>(status.mix_frames),
                static_cast<unsigned long long>(status.device_frames_written),
                static_cast<unsigned long long>(status.device_frames_played),
                static_cast<unsigned long long>(status.missing_frames));
}

}  // namespace

int main(int argc, char** argv) {
    out::OutputSettings settings;
    double seconds = 2.0;
    bool tone = false;
    out::WasapiOptions wasapi;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const std::string value = i + 1 < argc ? argv[i + 1] : "";
        if (arg == "--sink" && !value.empty()) {
            settings.sink = out::parse_output_settings("audio_backend = " + value).sink;
            ++i;
        } else if (arg == "--mode" && !value.empty()) {
            settings.sharing = out::parse_output_settings("audio_mode = " + value).sharing;
            ++i;
        } else if (arg == "--rate" && !value.empty()) {
            settings.device_rate = out::parse_output_settings("audio_rate = " + value).device_rate;
            ++i;
        } else if (arg == "--seconds" && !value.empty()) {
            seconds = std::atof(value.c_str());
            ++i;
        } else if (arg == "--bit-perfect") {
            settings.bit_perfect = true;
        } else if (arg == "--tone") {
            tone = true;
        } else if (arg == "--wasapi-convert") {
            wasapi.windows_converts = true;
        } else {
            std::fputs(kUsage, arg == "--help" || arg == "-h" ? stdout : stderr);
            return arg == "--help" || arg == "-h" ? 0 : 2;
        }
    }

    // The source counts what it is asked for: the device has to take the frames in order, none twice.
    std::atomic<std::uint64_t> rendered{0};
    const out::MixSource source = [&rendered, tone](std::int16_t* interleaved, std::uint32_t frames) {
        const std::uint64_t first = rendered.fetch_add(frames, std::memory_order_relaxed);
        for (std::uint32_t i = 0; i < frames; ++i) {
            const double phase = static_cast<double>((first + i) % out::kMixRate) * 440.0 / out::kMixRate;
            const auto sample = static_cast<std::int16_t>(tone ? 1500.0 * std::sin(phase * 6.283185307179586) : 0.0);
            interleaved[2 * i] = interleaved[2 * i + 1] = sample;
        }
        return frames;
    };

    out::SinkProvider sinks;
    sinks.make = [wasapi](out::OutputSink kind) {
        return kind == out::OutputSink::Wasapi ? out::make_wasapi_sink(wasapi) : out::make_platform_sink(kind);
    };
    sinks.virtual_time = [] { return false; };
    out::OutputDevice device(sinks);
    const out::OutputResult result = device.open(settings, source);
    std::printf("open: %s\n", out::name_of(result));
    print("opened", device.status());
    if (!out::playing(result)) return 1;

    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    const out::OutputStatus playing = device.status();
    device.close();
    print("after playing", playing);

    const std::uint64_t expected = static_cast<std::uint64_t>(seconds * 0.5 * out::kMixRate);
    const bool took_the_mix = playing.state == out::OutputState::Playing && playing.mix_frames == rendered.load() &&
                              playing.mix_frames >= expected && playing.missing_frames == 0;
    std::printf("%s\n", took_the_mix ? "OK: the device took the mix" : "FAILED: the device did not take the mix");
    return took_the_mix ? 0 : 1;
}
