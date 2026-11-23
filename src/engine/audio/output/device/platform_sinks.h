// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The sinks this build has: SDL's audio everywhere, WASAPI on Windows, the replay's VBlank sink.

#include "engine/audio/output/device_sink.h"

#include <memory>

namespace sh::audio::output {

std::unique_ptr<DeviceSink> make_sdl_sink();

struct WasapiOptions {
    // A shared device whose mixer does not run in the mix's format is opened all the same, with Windows
    // converting the stream. The game never asks for this (it falls back to SDL instead); sh-audio-check does,
    // to run the sink's event loop on machines whose mixer is floating point, which is all of them.
    bool windows_converts = false;
};
// Nothing on a platform other than Windows.
std::unique_ptr<DeviceSink> make_wasapi_sink(WasapiOptions options = {});

// A sink of a kind for SinkProvider::make. The VBlank sink keeps the mix in the file the environment variable
// SH_AUDIO_DUMP names, when it names one.
std::unique_ptr<DeviceSink> make_platform_sink(OutputSink kind);

// The output of the running game, on this build's sinks and the game's clock.
OutputDevice& game_output();

}  // namespace sh::audio::output

namespace sh::audio {

// The replay clock's listener: one vertical blank passed (engine/timing/vblank.cpp).
void render_virtual_vblank(int blanks_per_second);

}  // namespace sh::audio
