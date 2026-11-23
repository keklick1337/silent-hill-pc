// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/audio/output/device/platform_sinks.h"

#include "engine/audio/output/vblank_sink.h"

#include <cstdlib>
#include <filesystem>

namespace sh::audio::output {

#ifndef _WIN32
std::unique_ptr<DeviceSink> make_wasapi_sink(WasapiOptions) { return nullptr; }
#endif

std::unique_ptr<DeviceSink> make_platform_sink(OutputSink kind) {
    switch (kind) {
        case OutputSink::Sdl: return make_sdl_sink();
        case OutputSink::Wasapi: return make_wasapi_sink();
        case OutputSink::VBlank: {
            const char* capture = std::getenv("SH_AUDIO_DUMP");
            return std::make_unique<VBlankSink>(capture != nullptr ? std::filesystem::path(capture)
                                                                   : std::filesystem::path());
        }
        case OutputSink::Auto: break;
    }
    return nullptr;
}

}  // namespace sh::audio::output
