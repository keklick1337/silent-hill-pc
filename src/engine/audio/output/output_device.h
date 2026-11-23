// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The sound output: hands the engine's 44.1 kHz stereo mix to a device. OutputDevice chooses a sink (the
// platform's own device interface, SDL's, or the silent one a replay drives from its own clock), and the sink
// pulls the mix through a MixSource on the device's thread. The mix is never resampled or converted here: a
// device takes it as it is, or with every frame repeated at a whole multiple of the rate.

#include "engine/audio/output/device_format.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace sh::audio::output {

// Renders `frames` interleaved stereo frames of the mix and returns how many it wrote; what it leaves out
// plays as silence. Called on the device's thread.
using MixSource = std::function<std::uint32_t(std::int16_t* interleaved, std::uint32_t frames)>;

enum class OutputResult : int {
    Ok,
    // Playing, but not on the sink that was asked for; the status says why.
    OkFallback,
    InvalidSettings,
    AlreadyOpen,
    NoDevice,
    UnsupportedFormat,
    DeviceBusy,
    DeviceLost,
    SinkError,
    ThreadError,
};

inline bool playing(OutputResult result) { return result == OutputResult::Ok || result == OutputResult::OkFallback; }

// Which device interface plays the mix. Auto is the platform's own where there is one, then SDL.
enum class OutputSink : int { Auto, Wasapi, Sdl, VBlank };
// How the device is shared with other programs. Auto is Shared, or Exclusive when the settings insist on an
// untouched stream.
enum class OutputSharing : int { Auto, Shared, Exclusive };
enum class OutputState : int { Closed, Opening, Priming, Playing, Closing, Failed };

const char* name_of(OutputSink sink);
const char* name_of(OutputSharing sharing);
const char* name_of(OutputResult result);

// What the configuration asks of the output (config.cfg: audio_backend, audio_mode, audio_rate,
// audio_bit_perfect).
struct OutputSettings {
    OutputSink sink = OutputSink::Auto;
    OutputSharing sharing = OutputSharing::Auto;
    // The device's rate: the mix rate or a multiple of it (88200, 176400, 352800). 0 is the mix rate.
    std::uint32_t device_rate = 0;
    // Only an exclusive device at the mix rate will do; nothing else is tried.
    bool bit_perfect = false;
    // How much sound a device buffers, and the most the output renders ahead of it.
    std::uint32_t latency_ms = 20;
    std::uint32_t queue_frames = 8192;

    bool operator==(const OutputSettings&) const = default;
};

// Reads the output's keys from the text of a config.cfg; a later line replaces an earlier one, an unknown value
// is the default.
OutputSettings parse_output_settings(std::string_view config_text);

struct OutputStatus {
    OutputState state = OutputState::Closed;
    OutputResult result = OutputResult::Ok;
    OutputSink sink = OutputSink::Auto;
    OutputSharing sharing = OutputSharing::Auto;
    SampleFormat format = SampleFormat::Unknown;
    std::uint32_t device_rate = 0;
    // The device plays exactly the mix's samples: exclusive, at the mix rate.
    bool bit_perfect = false;
    // Sinks that failed before the one that plays, and why the last of them did.
    std::uint32_t fallbacks = 0;
    OutputResult fallback_reason = OutputResult::Ok;
    // The platform's error number of the last failure, when it gave one.
    std::int64_t native_error = 0;
    std::string device_name;
    std::string note;
    // Mix frames rendered; device frames written and (where the device tells) played; frames of silence written
    // because the mix was not there.
    std::uint64_t mix_frames = 0;
    std::uint64_t device_frames_written = 0;
    std::uint64_t device_frames_played = 0;
    std::uint64_t missing_frames = 0;
};

class DeviceSink;

// The sinks of the machine: `make` returns a new sink of a kind, or nothing when the platform has none of it;
// `virtual_time` tells whether the game runs on a replay's clock, which takes the VBlank sink whatever was asked.
struct SinkProvider {
    std::function<std::unique_ptr<DeviceSink>(OutputSink)> make;
    std::function<bool()> virtual_time;
};

class OutputDevice {
public:
    explicit OutputDevice(SinkProvider provider);
    ~OutputDevice();
    OutputDevice(const OutputDevice&) = delete;
    OutputDevice& operator=(const OutputDevice&) = delete;

    // Opens a sink for the settings and starts pulling `source`. AlreadyOpen while one plays.
    OutputResult open(const OutputSettings& settings, MixSource source);
    void close();
    OutputStatus status() const;

    // A replay's clock ticked: the VBlank sink, when it is the open one, renders that tick's share of the mix.
    void vblank(int blanks_per_second);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace sh::audio::output
