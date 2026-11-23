// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/audio/output/output_device.h"

namespace sh::audio::output {

namespace {

constexpr std::string_view kBlanks = " \t\r\n\v\f";

std::string_view trimmed(std::string_view text) {
    const std::size_t first = text.find_first_not_of(kBlanks);
    if (first == std::string_view::npos) return {};
    return text.substr(first, text.find_last_not_of(kBlanks) - first + 1);
}

// The whole number a value starts with ("88200 Hz" is 88200); 0 when it starts with none.
long leading_number(std::string_view text) {
    std::size_t at = 0;
    const bool negative = !text.empty() && text[0] == '-';
    if (!text.empty() && (text[0] == '-' || text[0] == '+')) at = 1;
    long value = 0;
    for (; at < text.size() && text[at] >= '0' && text[at] <= '9' && value < 100'000'000; ++at) {
        value = value * 10 + (text[at] - '0');
    }
    return negative ? -value : value;
}

}  // namespace

OutputSettings parse_output_settings(std::string_view config_text) {
    OutputSettings settings;
    while (!config_text.empty()) {
        const std::size_t line_end = config_text.find('\n');
        const std::string_view line = trimmed(config_text.substr(0, line_end));
        config_text.remove_prefix(line_end == std::string_view::npos ? config_text.size() : line_end + 1);

        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        const std::string_view key = trimmed(line.substr(0, equals));
        std::string_view value = line.substr(equals + 1);
        // A comment may follow the value on its line.
        value = trimmed(value.substr(0, value.find_first_of("#;")));

        if (key == "audio_backend") {
            settings.sink = value == "wasapi" ? OutputSink::Wasapi : value == "sdl" ? OutputSink::Sdl : OutputSink::Auto;
        } else if (key == "audio_mode") {
            settings.sharing = value == "exclusive" ? OutputSharing::Exclusive
                               : value == "shared"  ? OutputSharing::Shared
                                                    : OutputSharing::Auto;
        } else if (key == "audio_rate") {
            const long rate = value == "auto" ? 0 : leading_number(value);
            settings.device_rate = rate > 0 && rate_multiple(static_cast<std::uint32_t>(rate)) != 0
                                       ? static_cast<std::uint32_t>(rate)
                                       : 0;
        } else if (key == "audio_bit_perfect") {
            settings.bit_perfect = leading_number(value) != 0;
        }
    }
    return settings;
}

const char* name_of(OutputSink sink) {
    switch (sink) {
        case OutputSink::Auto: return "auto";
        case OutputSink::Wasapi: return "wasapi";
        case OutputSink::Sdl: return "sdl";
        case OutputSink::VBlank: return "replay clock";
    }
    return "?";
}

const char* name_of(OutputSharing sharing) {
    switch (sharing) {
        case OutputSharing::Auto: return "auto";
        case OutputSharing::Shared: return "shared";
        case OutputSharing::Exclusive: return "exclusive";
    }
    return "?";
}

const char* name_of(OutputResult result) {
    switch (result) {
        case OutputResult::Ok: return "ok";
        case OutputResult::OkFallback: return "ok, on a fallback sink";
        case OutputResult::InvalidSettings: return "invalid settings";
        case OutputResult::AlreadyOpen: return "already open";
        case OutputResult::NoDevice: return "no device";
        case OutputResult::UnsupportedFormat: return "unsupported format";
        case OutputResult::DeviceBusy: return "device busy";
        case OutputResult::DeviceLost: return "device lost";
        case OutputResult::SinkError: return "sink error";
        case OutputResult::ThreadError: return "thread error";
    }
    return "?";
}

}  // namespace sh::audio::output
